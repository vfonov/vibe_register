#include "NiftiVolume.h"
#include "Volume.h"
#include <nifti1_io.h>
#include <znzlib.h>
#include <stdexcept>
#include <iostream>
#include <cmath>
#include <limits>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

// Helper: determine if file is NIfTI by extension
bool isNiftiFile(const std::string& filename)
{
    if (filename.size() >= 7) {
        std::string ext7 = filename.substr(filename.size() - 7);
        if (ext7 == ".nii.gz") return true;
    }
    if (filename.size() >= 4) {
        std::string ext4 = filename.substr(filename.size() - 4);
        if (ext4 == ".nii") return true;
    }
    return false;
}

// Internal helper: load NIfTI data into Volume structure
void loadNiftiIntoVolume(const std::string& filename, Volume& vol)
{
    // Read NIfTI image with data
    nifti_image* nii_ptr = nifti_image_read(filename.c_str(), 1);
    
    if (!nii_ptr) {
        throw std::runtime_error("Failed to read NIfTI file: " + filename);
    }
    
    // NIfTI file dimensions (before any axis permutation)
    int nii_dims[3] = { nii_ptr->nx, nii_ptr->ny, nii_ptr->nz };

    if (nii_dims[0] <= 0 || nii_dims[1] <= 0 || nii_dims[2] <= 0)
    {
        nifti_image_free(nii_ptr);
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
    // (0=xspace, 1=yspace, 2=zspace) it most closely aligns with by
    // finding the largest absolute component in column j of the affine.
    int spatial_axes[3];
    for (int j = 0; j < 3; ++j)
    {
        float cx = std::fabs(nii_xfm.m[0][j]);
        float cy = std::fabs(nii_xfm.m[1][j]);
        float cz = std::fabs(nii_xfm.m[2][j]);
        if (cy > cx && cy > cz)
            spatial_axes[j] = 1;
        else if (cz > cx && cz > cy)
            spatial_axes[j] = 2;
        else
            spatial_axes[j] = 0;
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

    // Extract voxel data
    size_t total_voxels = nii_ptr->nvox;
    vol.data.resize(total_voxels);
    
    // Convert NIfTI data type to float
    // NIfTI stores data in various formats; we convert to float
    switch (nii_ptr->datatype) {
    case DT_INT8:
    case DT_UINT8:
        {
            unsigned char* src = static_cast<unsigned char*>(nii_ptr->data);
            for (size_t i = 0; i < total_voxels; ++i) {
                vol.data[i] = static_cast<float>(src[i]);
            }
        }
        break;
    
    case DT_INT16:
    case DT_UINT16:
        {
            unsigned short* src = static_cast<unsigned short*>(nii_ptr->data);
            for (size_t i = 0; i < total_voxels; ++i) {
                vol.data[i] = static_cast<float>(src[i]);
            }
        }
        break;
    
    case DT_INT32:
    case DT_UINT32:
        {
            unsigned int* src = static_cast<unsigned int*>(nii_ptr->data);
            for (size_t i = 0; i < total_voxels; ++i) {
                vol.data[i] = static_cast<float>(src[i]);
            }
        }
        break;
    
    case DT_FLOAT32:
        {
            float* src = static_cast<float*>(nii_ptr->data);
            for (size_t i = 0; i < total_voxels; ++i) {
                vol.data[i] = src[i];
            }
        }
        break;
    
    case DT_FLOAT64:
        {
            double* src = static_cast<double*>(nii_ptr->data);
            for (size_t i = 0; i < total_voxels; ++i) {
                vol.data[i] = static_cast<float>(src[i]);
            }
        }
        break;
    
    default:
        nifti_image_free(nii_ptr);
        throw std::runtime_error("Unsupported NIfTI datatype: " + 
                                 std::to_string(nii_ptr->datatype));
    }
    
    // Calculate min/max for visualization
    vol.min_value = std::numeric_limits<float>::max();
    vol.max_value = std::numeric_limits<float>::lowest();
    
    for (float v : vol.data) {
        if (v < vol.min_value) vol.min_value = v;
        if (v > vol.max_value) vol.max_value = v;
    }
    
    if (vol.min_value >= vol.max_value) {
        vol.max_value = vol.min_value + 1.0f;
    }
    
    // Apply scl_slope/scl_inter if present
    if (nii_ptr->scl_slope != 0.0f) {
        for (float& v : vol.data) {
            v = v * nii_ptr->scl_slope + nii_ptr->scl_inter;
        }
        // Recalculate min/max
        vol.min_value = vol.min_value * nii_ptr->scl_slope + nii_ptr->scl_inter;
        vol.max_value = vol.max_value * nii_ptr->scl_slope + nii_ptr->scl_inter;
    }

    // Stage 6: Transpose and/or flip voxel data to match MINC axis order.
    // NIfTI data is stored as array[k][j][i] (file axis 0 = fastest).
    // MINC expects data[z][y][x] where z=zspace, y=yspace, x=xspace.
    //
    // Two transformations may be needed:
    //   (a) Axis permutation — if spatial_axes is not identity
    //   (b) Axis reversal — for each axis where flipAxis[d] is true
    //       (negative step was normalized to positive, data must be mirrored)
    bool needTranspose = (spatial_axes[0] != 0 ||
                          spatial_axes[1] != 1 ||
                          spatial_axes[2] != 2);
    bool needFlip = (flipAxis[0] || flipAxis[1] || flipAxis[2]);

    if (needTranspose || needFlip)
    {
        int mx = vol.dimensions.x;
        int my = vol.dimensions.y;
        int mz = vol.dimensions.z;

        std::vector<float> reordered(total_voxels);

        for (int z = 0; z < mz; ++z)
        {
            for (int y = 0; y < my; ++y)
            {
                for (int x = 0; x < mx; ++x)
                {
                    // Destination MINC indices (after flip)
                    int mnc[3] = { x, y, z };

                    // Apply axis reversal: flip MINC index for axes with
                    // negative original step (now positive after normalization).
                    // This mirrors the data so voxel 0 maps to the new start.
                    int src_mnc[3] = {
                        flipAxis[0] ? (mx - 1 - mnc[0]) : mnc[0],
                        flipAxis[1] ? (my - 1 - mnc[1]) : mnc[1],
                        flipAxis[2] ? (mz - 1 - mnc[2]) : mnc[2]
                    };

                    // Map from (possibly permuted) MINC index to NIfTI file index
                    int ni = src_mnc[spatial_axes[0]];
                    int nj = src_mnc[spatial_axes[1]];
                    int nk = src_mnc[spatial_axes[2]];

                    size_t src_idx = static_cast<size_t>(nk) * nii_dims[1] * nii_dims[0]
                                   + static_cast<size_t>(nj) * nii_dims[0]
                                   + ni;
                    size_t dst_idx = static_cast<size_t>(z) * my * mx
                                   + static_cast<size_t>(y) * mx
                                   + x;
                    reordered[dst_idx] = vol.data[src_idx];
                }
            }
        }
        vol.data = std::move(reordered);
    }

    nifti_image_free(nii_ptr);
}

// Wrapper function for Volume::load()
void loadNiftiFile(const std::string& filename, Volume& vol)
{
    loadNiftiIntoVolume(filename, vol);
}
