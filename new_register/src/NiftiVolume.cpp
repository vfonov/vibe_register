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
    
    // Extract dimensions (NIfTI stores X, Y, Z in order)
    vol.dimensions = glm::ivec3(nii_ptr->nx, nii_ptr->ny, nii_ptr->nz);
    
    if (vol.dimensions.x <= 0 || vol.dimensions.y <= 0 || vol.dimensions.z <= 0) {
        nifti_image_free(nii_ptr);
        throw std::runtime_error("Invalid NIfTI dimensions: " + filename);
    }
    
    // Handle unit conversions (NIfTI stores xyz_units)
    double unit_scale = 1.0;
    switch (nii_ptr->xyz_units) {
    case NIFTI_UNITS_METER:
        unit_scale = 1000.0;  // meters → millimeters
        break;
    case NIFTI_UNITS_MM:
    case NIFTI_UNITS_UNKNOWN:
        unit_scale = 1.0;  // already in mm
        break;
    case NIFTI_UNITS_MICRON:
        unit_scale = 0.001;  // microns → millimeters
        break;
    default:
        std::cerr << "Warning: Unknown NIfTI xyz_units code " 
                  << nii_ptr->xyz_units << ", assuming mm\n";
        break;
    }
    
    // Extract pixdim (voxel spacing)
    vol.step = glm::dvec3(
        nii_ptr->dx * unit_scale,
        nii_ptr->dy * unit_scale,
        nii_ptr->dz * unit_scale
    );
    
    // Determine which transform to use: s-form preferred, q-form fallback
    mat44 nii_xfm;
    if (nii_ptr->sform_code != NIFTI_XFORM_UNKNOWN) {
        nii_xfm = nii_ptr->sto_xyz;
    } else if (nii_ptr->qform_code != NIFTI_XFORM_UNKNOWN) {
        nii_xfm = nii_ptr->qto_xyz;
    } else {
        // No valid transform - create identity matrix manually
        nii_xfm.m[0][0] = nii_ptr->dx;
        nii_xfm.m[1][1] = nii_ptr->dy;
        nii_xfm.m[2][2] = nii_ptr->dz;
        nii_xfm.m[0][1] = nii_xfm.m[0][2] = nii_xfm.m[1][0] = 
        nii_xfm.m[1][2] = nii_xfm.m[2][0] = nii_xfm.m[2][1] = 0.0f;
        nii_xfm.m[0][3] = nii_xfm.m[1][3] = nii_xfm.m[2][3] = 0.0f;
        nii_xfm.m[3][0] = nii_xfm.m[3][1] = nii_xfm.m[3][2] = 0.0f;
        nii_xfm.m[3][3] = 1.0f;
    }
    
    // NIfTI uses RAS coordinate system. We need to extract the affine
    // transform and convert to MINC format (direction cosines + starts/steps)
    
    // The NIfTI 4×4 matrix maps voxel indices (i,j,k) to world (x,y,z) in RAS
    // world = nii_xfm * [i, j, k, 1]^T
    
    // Extract rotation/scaling part (3×3 upper-left)
    glm::dmat3 affine(
        nii_xfm.m[0][0], nii_xfm.m[0][1], nii_xfm.m[0][2],
        nii_xfm.m[1][0], nii_xfm.m[1][1], nii_xfm.m[1][2],
        nii_xfm.m[2][0], nii_xfm.m[2][1], nii_xfm.m[2][2]
    );
    
    // Extract translation (world coordinate of voxel 0,0,0)
    glm::dvec3 translation(
        nii_xfm.m[0][3],
        nii_xfm.m[1][3],
        nii_xfm.m[2][3]
    );
    
    // Decompose affine into direction cosines and step sizes
    // The columns of the affine matrix give the world-space direction of each voxel axis
    for (int axis = 0; axis < 3; ++axis) {
        glm::dvec3 col(affine[0][axis], affine[1][axis], affine[2][axis]);
        double len = glm::length(col);
        
        if (len > 1e-12) {
            vol.dirCos[axis] = col / len;  // Unit vector
            vol.step[axis] = len * unit_scale;  // Voxel size along this axis
        } else {
            // Degenerate case - use identity
            vol.dirCos[axis] = glm::dvec3(0.0);
            vol.dirCos[axis][axis] = 1.0;
            vol.step[axis] = 1.0;
        }
    }
    
    // Calculate start (world coordinate of first voxel center)
    // In MINC, voxelToWorld = dirCos * diag(step) * voxel + dirCos * start + translation
    // But NIfTI already gives us the full transform, so we use it directly
    vol.voxelToWorld = glm::dmat4(
        glm::dvec4(affine[0][0], affine[1][0], affine[2][0], 0.0),
        glm::dvec4(affine[0][1], affine[1][1], affine[2][1], 0.0),
        glm::dvec4(affine[0][2], affine[1][2], affine[2][2], 0.0),
        glm::dvec4(translation.x, translation.y, translation.z, 1.0)
    );
    
    // Apply unit scale to translation
    vol.voxelToWorld[3] = glm::dvec4(
        translation.x * unit_scale,
        translation.y * unit_scale,
        translation.z * unit_scale,
        1.0
    );
    
    vol.worldToVoxel = glm::inverse(vol.voxelToWorld);
    
    // Calculate start from corner voxel (0,0,0)
    glm::dvec3 corner_world;
    vol.transformVoxelToWorld(glm::ivec3(0, 0, 0), corner_world);
    vol.start = corner_world;
    
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
    
    nifti_image_free(nii_ptr);
}

// Wrapper function for Volume::load()
void loadNiftiFile(const std::string& filename, Volume& vol)
{
    loadNiftiIntoVolume(filename, vol);
}
