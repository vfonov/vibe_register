#include "NiftiVolume.h"
#include "Volume.h"
#include "VolumeReorient.h"
#include <nifti1_io.h>
#include <znzlib.h>
#include <array>
#include <stdexcept>
#include <iostream>
#include <cmath>
#include <limits>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace
{

bool endsWithNoCase(const std::string& s, const std::string& suffix)
{
    if (s.size() < suffix.size())
        return false;
    return std::equal(suffix.begin(), suffix.end(), s.end() - static_cast<std::ptrdiff_t>(suffix.size()),
                      [](char a, char b)
                      {
                          return std::tolower(static_cast<unsigned char>(a)) ==
                                 std::tolower(static_cast<unsigned char>(b));
                      });
}

/// Convert the first `count` voxels of type T to float.
template <typename T>
void convertVoxels(const void* src, size_t count, std::vector<float>& dst)
{
    const T* in = static_cast<const T*>(src);
    for (size_t i = 0; i < count; ++i)
        dst[i] = static_cast<float>(in[i]);
}

bool isPairHeaderName(const std::string& s)
{
    return endsWithNoCase(s, ".hdr") || endsWithNoCase(s, ".hdr.gz");
}

bool isPairImageName(const std::string& s)
{
    return endsWithNoCase(s, ".img") || endsWithNoCase(s, ".img.gz");
}

/// `filename` without its .hdr / .img (and optional .gz) extension.
std::string pairBaseName(const std::string& filename)
{
    std::string base = filename;
    if (endsWithNoCase(base, ".gz"))
        base.erase(base.size() - 3);
    base.erase(base.size() - 4);
    return base;
}

/// A two-file image needs both halves. niftilib only reports a generic read
/// failure when one is absent, so look for the partner first and name it.
void requirePairPartner(const std::string& filename)
{
    using CString = std::unique_ptr<char, decltype(&std::free)>;
    const bool headerGiven = isPairHeaderName(filename);
    if (!headerGiven && !isPairImageName(filename))
        return;

    const std::string base = pairBaseName(filename);
    if (!headerGiven)
    {
        CString header(nifti_findhdrname(filename.c_str()), &std::free);
        if (!header)
            throw std::runtime_error("NIfTI header file not found for image: " + filename +
                                     " (expected " + base + ".hdr or .hdr.gz)");
    }
    CString image(nifti_findimgname(filename.c_str(), NIFTI_FTYPE_NIFTI1_2), &std::free);
    if (!image)
        throw std::runtime_error("NIfTI image file not found for header: " + filename +
                                 " (expected " + base + ".img or .img.gz)");
}

} // namespace

// Helper: determine if file is NIfTI (or an ANALYZE-style .hdr/.img pair) by extension
bool isNiftiFile(const std::string& filename)
{
    return endsWithNoCase(filename, ".nii.gz") || endsWithNoCase(filename, ".nii") ||
           isPairHeaderName(filename) || isPairImageName(filename);
}

// Internal helper: load NIfTI data into Volume structure
void loadNiftiIntoVolume(const std::string& filename, Volume& vol)
{
    requirePairPartner(filename);

    // Read NIfTI image with data; freed on every exit path.
    std::unique_ptr<nifti_image, decltype(&nifti_image_free)> nii(
        nifti_image_read(filename.c_str(), 1), &nifti_image_free);

    if (!nii) {
        throw std::runtime_error("Failed to read NIfTI file: " + filename);
    }
    nifti_image* nii_ptr = nii.get();

    // NIfTI file dimensions (before any axis permutation)
    int nii_dims[3] = { nii_ptr->nx, nii_ptr->ny, nii_ptr->nz };

    if (nii_dims[0] <= 0 || nii_dims[1] <= 0 || nii_dims[2] <= 0)
    {
        throw std::runtime_error("Invalid NIfTI dimensions: " + filename);
    }

    // Handle unit conversions (NIfTI stores xyz_units)
    double unit_scale = 1.0;
    switch (nii_ptr->xyz_units)
    {
    case NIFTI_UNITS_METER:
        unit_scale = 1000.0;  // meters → millimeters
        break;
    case NIFTI_UNITS_MM:
    case NIFTI_UNITS_UNKNOWN:
        unit_scale = 1.0;
        break;
    case NIFTI_UNITS_MICRON:
        unit_scale = 0.001;  // microns → millimeters
        break;
    default:
        std::cerr << "Warning: Unknown NIfTI xyz_units code "
                  << nii_ptr->xyz_units << ", assuming mm\n";
        break;
    }

    // Determine which transform to use: s-form preferred, q-form fallback
    mat44 nii_xfm;
    if (nii_ptr->sform_code != NIFTI_XFORM_UNKNOWN)
    {
        nii_xfm = nii_ptr->sto_xyz;
    }
    else if (nii_ptr->qform_code != NIFTI_XFORM_UNKNOWN)
    {
        nii_xfm = nii_ptr->qto_xyz;
    }
    else
    {
        if (nii_ptr->nifti_type == NIFTI_FTYPE_ANALYZE)
        {
            std::cerr << "Warning: " << filename << " is an ANALYZE 7.5 image; its orientation "
                      << "and origin are unknown, assuming voxel sizes with origin 0\n";
        }
        // No valid transform — create identity matrix from pixdim
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                nii_xfm.m[r][c] = 0.0f;
        nii_xfm.m[0][0] = nii_ptr->dx;
        nii_xfm.m[1][1] = nii_ptr->dy;
        nii_xfm.m[2][2] = nii_ptr->dz;
        nii_xfm.m[3][3] = 1.0f;
    }

    // ----------------------------------------------------------------
    // NIfTI → MINC coordinate conversion (replicates nii2mnc algorithm).
    // See research.md §11.3 for the full mathematical derivation.
    // ----------------------------------------------------------------

    // Stage 1: Spatial axis permutation.
    // For each NIfTI file axis j, determine which MINC spatial dimension
    // (0=xspace, 1=yspace, 2=zspace) it most closely aligns with. Assign
    // greedily by the largest absolute component still available, so the
    // result is always a permutation even for oblique affines with ties.
    int spatial_axes[3] = { 0, 1, 2 };
    bool rowUsed[3] = { false, false, false };
    bool colUsed[3] = { false, false, false };
    for (int n = 0; n < 3; ++n)
    {
        int bestRow = -1;
        int bestCol = -1;
        float best = -1.0f;
        for (int j = 0; j < 3; ++j)
        {
            if (colUsed[j])
                continue;
            for (int row = 0; row < 3; ++row)
            {
                if (rowUsed[row])
                    continue;
                float c = std::fabs(nii_xfm.m[row][j]);
                if (c > best)
                {
                    best = c;
                    bestRow = row;
                    bestCol = j;
                }
            }
        }
        spatial_axes[bestCol] = bestRow;
        rowUsed[bestRow] = true;
        colUsed[bestCol] = true;
    }

    // Stage 2: Permute columns into MINC canonical order.
    // After this, column d of perm corresponds to MINC spatial axis d.
    double perm[3][3];
    double origin[3];
    for (int j = 0; j < 3; ++j)
    {
        for (int row = 0; row < 3; ++row)
        {
            perm[row][spatial_axes[j]] = static_cast<double>(nii_xfm.m[row][j]);
        }
    }
    for (int row = 0; row < 3; ++row)
    {
        origin[row] = static_cast<double>(nii_xfm.m[row][3]) * unit_scale;
    }

    // Permute dimensions to match MINC axis order.
    int mnc_dims[3];
    for (int j = 0; j < 3; ++j)
    {
        mnc_dims[spatial_axes[j]] = nii_dims[j];
    }
    vol.dimensions = glm::ivec3(mnc_dims[0], mnc_dims[1], mnc_dims[2]);

    // Stage 3: Decompose into dirCos, step, start.
    // Replicates convert_transform_to_starts_and_steps() from libminc.
    for (int d = 0; d < 3; ++d)
    {
        // Column d of perm = scaled direction vector for spatial axis d
        double ax[3] = { perm[0][d], perm[1][d], perm[2][d] };

        double mag = std::sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
        if (mag <= 0.0)
            mag = 1.0;

        // Sign from diagonal element (MINC convention)
        double sign = (ax[d] < 0.0) ? -1.0 : 1.0;

        vol.step[d] = sign * mag * unit_scale;

        vol.dirCos[d][0] = ax[0] / (sign * mag);
        vol.dirCos[d][1] = ax[1] / (sign * mag);
        vol.dirCos[d][2] = ax[2] / (sign * mag);
    }

    // Solve C · start = origin for start (3×3 linear system).
    // C = [dirCos[0] | dirCos[1] | dirCos[2]] as columns in GLM.
    glm::dmat3 dirCos3(
        vol.dirCos[0][0], vol.dirCos[0][1], vol.dirCos[0][2],
        vol.dirCos[1][0], vol.dirCos[1][1], vol.dirCos[1][2],
        vol.dirCos[2][0], vol.dirCos[2][1], vol.dirCos[2][2]
    );
    glm::dvec3 originVec(origin[0], origin[1], origin[2]);
    glm::dvec3 startVec = glm::inverse(dirCos3) * originVec;
    vol.start = startVec;

    // Stage 4: Normalize negative steps.
    // MINC's minc2_setup_standard_order() enforces positive steps.
    // When a step is negative, we must:
    //   1. Flip the step sign to positive
    //   2. Shift start to the opposite end of the axis
    //   3. Mark that axis for data reversal (done after data load)
    // dirCos stays the same since the decomposition already factored the sign.
    bool flipAxis[3] = { false, false, false };
    for (int d = 0; d < 3; ++d)
    {
        if (vol.step[d] < 0.0)
        {
            flipAxis[d] = true;
            // start shifts to the far end: new_start = start + step * (N-1)
            vol.start[d] = vol.start[d] + vol.step[d] * (vol.dimensions[d] - 1);
            vol.step[d]  = -vol.step[d];
        }
    }

    // Build voxelToWorld from MINC components (same formula as Volume.cpp).
    // M₃ₓ₃ = dirCos3 * diag(step),  T = dirCos3 * start
    glm::dmat3 affine3 = dirCos3;
    for (int i = 0; i < 3; ++i)
        affine3[i] *= vol.step[i];

    glm::dvec3 trans = dirCos3 * glm::dvec3(vol.start.x, vol.start.y, vol.start.z);

    vol.voxelToWorld = glm::dmat4(
        glm::dvec4(affine3[0], 0.0),
        glm::dvec4(affine3[1], 0.0),
        glm::dvec4(affine3[2], 0.0),
        glm::dvec4(trans, 1.0)
    );
    vol.worldToVoxel = glm::inverse(vol.voxelToWorld);

    // Extract voxel data: only the first 3-D frame of a 4-D (or higher) image.
    const size_t total_voxels = static_cast<size_t>(nii_dims[0]) * nii_dims[1] * nii_dims[2];
    vol.data.resize(total_voxels);

    // Convert NIfTI data type to float
    switch (nii_ptr->datatype) {
    case DT_INT8:    convertVoxels<int8_t>(nii_ptr->data, total_voxels, vol.data);   break;
    case DT_UINT8:   convertVoxels<uint8_t>(nii_ptr->data, total_voxels, vol.data);  break;
    case DT_INT16:   convertVoxels<int16_t>(nii_ptr->data, total_voxels, vol.data);  break;
    case DT_UINT16:  convertVoxels<uint16_t>(nii_ptr->data, total_voxels, vol.data); break;
    case DT_INT32:   convertVoxels<int32_t>(nii_ptr->data, total_voxels, vol.data);  break;
    case DT_UINT32:  convertVoxels<uint32_t>(nii_ptr->data, total_voxels, vol.data); break;
    case DT_INT64:   convertVoxels<int64_t>(nii_ptr->data, total_voxels, vol.data);  break;
    case DT_UINT64:  convertVoxels<uint64_t>(nii_ptr->data, total_voxels, vol.data); break;
    case DT_FLOAT32: convertVoxels<float>(nii_ptr->data, total_voxels, vol.data);    break;
    case DT_FLOAT64: convertVoxels<double>(nii_ptr->data, total_voxels, vol.data);   break;
    default:
        throw std::runtime_error("Unsupported NIfTI datatype: " +
                                 std::to_string(nii_ptr->datatype));
    }

    // Apply scl_slope/scl_inter if present (slope 0 means "no scaling").
    // The value range is computed afterwards by Volume::load().
    if (nii_ptr->scl_slope != 0.0f && std::isfinite(nii_ptr->scl_slope) &&
        std::isfinite(nii_ptr->scl_inter))
    {
        for (float& v : vol.data) {
            v = v * nii_ptr->scl_slope + nii_ptr->scl_inter;
        }
    }

    // Stage 6: Transpose and/or flip voxel data to match MINC axis order.
    // NIfTI data is stored as array[k][j][i] (file axis 0 = fastest).
    // MINC expects data[z][y][x] where z=zspace, y=yspace, x=xspace.
    //
    // Two transformations may be needed:
    //   (a) Axis permutation — if spatial_axes is not identity
    //   (b) Axis reversal — for each axis where flipAxis[d] is true
    //       (negative step was normalized to positive, data must be mirrored)
    const std::array<int, 3> fileAxisToSpatial{spatial_axes[0], spatial_axes[1], spatial_axes[2]};
    const std::array<bool, 3> flip{flipAxis[0], flipAxis[1], flipAxis[2]};
    if (!isIdentityReorient(fileAxisToSpatial, flip))
    {
        vol.data = reorientToStandard(vol.data.data(),
                                      {nii_dims[0], nii_dims[1], nii_dims[2]},
                                      fileAxisToSpatial, flip);
    }
}

// Wrapper function for Volume::load()
void loadNiftiFile(const std::string& filename, Volume& vol)
{
    loadNiftiIntoVolume(filename, vol);
}
