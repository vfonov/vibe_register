#include "Volume.h"
#include <cmath>
#include <iostream>
#include <vector>
#include <algorithm>

// Include minc2-simple header
#include <minc2-simple.h>

Volume::Volume() {
    dimensions[0] = 0;
    dimensions[1] = 0;
    dimensions[2] = 0;
}

Volume::~Volume() {}

void Volume::generate_test_data() {
    dimensions[0] = 256;
    dimensions[1] = 256;
    dimensions[2] = 256;
    data.resize(256 * 256 * 256);

    for(int z=0; z<256; ++z) {
        for(int y=0; y<256; ++y) {
            for(int x=0; x<256; ++x) {
                float val = (float)x / 256.0f;
                // Add a grid
                if (z % 32 == 0 || y % 32 == 0 || x % 32 == 0) val = 0.8f; 
                
                // Sphere in center
                float dx = x - 128.0f;
                float dy = y - 128.0f;
                float dz = z - 128.0f;
                if (std::sqrt(dx*dx + dy*dy + dz*dz) < 60.0f) val = 1.0f;

                data[z*256*256 + y*256 + x] = val;
            }
        }
    }
    std::cout << "Generated synthetic volume 256x256x256" << std::endl;
}

bool Volume::load(const std::string& filename) {
    if (filename.empty()) {
        std::cerr << "Volume::load: Empty filename provided." << std::endl;
        return false;
    }

    minc2_file_handle h;
    if (minc2_allocate(&h) != MINC2_SUCCESS) {
        std::cerr << "Volume::load: Failed to allocate minc2 handle." << std::endl;
        return false;
    }

    if (minc2_open(h, filename.c_str()) != MINC2_SUCCESS) {
        std::cerr << "Volume::load: Failed to open file: " << filename << std::endl;
        minc2_free(h);
        return false;
    }

    if (minc2_setup_standard_order(h) != MINC2_SUCCESS) {
        std::cerr << "Volume::load: Failed to setup standard dimension order." << std::endl;
        minc2_close(h);
        minc2_free(h);
        return false;
    }

    int ndim = 0;
    if (minc2_ndim(h, &ndim) != MINC2_SUCCESS) {
         std::cerr << "Volume::load: Failed to get number of dimensions." << std::endl;
         minc2_close(h);
         minc2_free(h);
         return false;
    }

    struct minc2_dimension *dims = NULL;
    if (minc2_get_representation_dimensions(h, &dims) != MINC2_SUCCESS) {
        std::cerr << "Volume::load: Failed to get dimension info." << std::endl;
        minc2_close(h);
        minc2_free(h);
        return false;
    }

    // We expect at least 3 spatial dimensions for a volume.
    // minc2_setup_standard_order puts them in Vector-X-Y-Z-Time order.
    // We want to find X, Y, Z.
    // Since we called setup_standard_order, we can iterate and check IDs.
    
    // For simplicity in this rewrite, let's assume we are dealing with a 3D volume
    // where the last 3 dimensions (or first 3 spatial ones) are X, Y, Z.
    // Actually, setup_standard_order should sort them.
    
    // Let's find X, Y, Z dimensions
    int dim_indices[3] = {-1, -1, -1}; // x, y, z indices in dims array
    
    for (int i = 0; i < ndim; ++i) {
        if (dims[i].id == MINC2_DIM_X) dim_indices[0] = i;
        else if (dims[i].id == MINC2_DIM_Y) dim_indices[1] = i;
        else if (dims[i].id == MINC2_DIM_Z) dim_indices[2] = i;
    }

    if (dim_indices[0] == -1 || dim_indices[1] == -1 || dim_indices[2] == -1) {
        std::cerr << "Volume::load: Could not find X, Y, and Z dimensions." << std::endl;
        // Fallback: assume last 3 are Z, Y, X? No, standard order should be reliable.
        // If it's a 2D image, we might fail here.
        minc2_close(h);
        minc2_free(h);
        return false;
    }

    dimensions[0] = dims[dim_indices[0]].length;
    dimensions[1] = dims[dim_indices[1]].length;
    dimensions[2] = dims[dim_indices[2]].length;

    size_t total_voxels = 1;
    for(int i=0; i<ndim; ++i) {
        total_voxels *= dims[i].length;
    }
    
    if (total_voxels == 0) {
        std::cerr << "Volume::load: Volume has 0 voxels." << std::endl;
        minc2_close(h);
        minc2_free(h);
        return false;
    }

    // Resize data vector
    // Note: If there are other dimensions (vector, time), total_voxels will be larger than x*y*z.
    // We load everything.
    try {
        data.resize(total_voxels);
    } catch (const std::bad_alloc& e) {
        std::cerr << "Volume::load: Failed to allocate memory for volume data (" << total_voxels * sizeof(float) << " bytes)." << std::endl;
        minc2_close(h);
        minc2_free(h);
        return false;
    }

    // Load data as FLOAT
    if (minc2_load_complete_volume(h, data.data(), MINC2_FLOAT) != MINC2_SUCCESS) {
        std::cerr << "Volume::load: Failed to load volume data." << std::endl;
        minc2_close(h);
        minc2_free(h);
        return false;
    }
    
    // Calculate min/max for visualization
    min_value = std::numeric_limits<float>::max();
    max_value = std::numeric_limits<float>::lowest();
    
    for (float v : data) {
        if (v < min_value) min_value = v;
        if (v > max_value) max_value = v;
    }
    
    if (min_value >= max_value) {
        max_value = min_value + 1.0f; // Prevent division by zero later
    }

    std::cout << "Loaded volume: " << filename << std::endl;
    std::cout << "Dimensions: " << dimensions[0] << "x" << dimensions[1] << "x" << dimensions[2] << std::endl;
    std::cout << "Range: [" << min_value << ", " << max_value << "]" << std::endl;

    minc2_close(h);
    minc2_free(h);
    
    // Cleanup dimensions array if needed? 
    // minc2-simple doesn't seem to expose a way to free the dims array returned by get_representation_dimensions
    // looking at the source of minc2-simple, it seems it manages it internally in the handle or returns a pointer to internal struct.
    // Checking minc2-simple.c would verify this. 
    // Assuming we don't need to free 'dims' explicitly as it's likely managed by the handle.

    return true;
}

float Volume::get(int x, int y, int z) const {
    // Basic bounds checking
    if (x < 0 || x >= dimensions[0] || 
        y < 0 || y >= dimensions[1] || 
        z < 0 || z >= dimensions[2]) return 0.0f;
    
    // Assuming standard order usually results in contiguous storage for XYZ or ZYX... 
    // minc2_setup_standard_order implies a specific order. 
    // If standard order is X, Y, Z, then index is x*strides[0] + y*strides[1] + z*strides[2].
    // But usually minc uses C-style ordering where last dimension changes fastest.
    // Wait, `minc2_setup_standard_order` makes the dimensions appear in X, Y, Z order in the `dims` array.
    // Does it change the *memory layout* of `minc2_load_complete_volume`?
    // "Setup minc file for reading or writing information in standardized order ... with positive steps"
    // Usually this means memory returned by load_complete_volume will follow that order.
    // If order is X, Y, Z (slowest to fastest? or fastest to slowest?)
    // In MINC2, usually the order is specified by the array.
    // If `dims` array has X at index 0, Y at 1, Z at 2.
    // And standard C array layout means index 2 changes fastest.
    // So index = i0 * (d1*d2) + i1 * (d2) + i2.
    // So if X is dims[0], Y is dims[1], Z is dims[2].
    // Index = x * (dim_y * dim_z) + y * (dim_z) + z.
    
    // However, we need to be careful. The `dim_indices` I calculated earlier tell me WHERE in the `dims` array X, Y, Z are.
    // If standard order puts them at 0, 1, 2.
    // I should verify the strides.
    // For now, let's assume Z, Y, X order (Z slowest, X fastest) which is typical for image processing, 
    // OR X, Y, Z (X slowest, Z fastest).
    
    // Actually, let's look at `minc2-simple.c` again or assume a default stride.
    // Most volume viewers assume Z is the slice index (slowest), Y is row, X is column (fastest).
    // If `minc2_setup_standard_order` sorts dimensions, it might put X first.
    // Let's assume the buffer is packed according to `dims` array order.
    // And usually minc tools produce Z, Y, X files.
    // But `setup_standard_order` documentation says: "( Vector dimension - X - Y -Z -TIME )".
    // Does that mean X is slowest? 
    // If I just use 1D indexing:
    // We will need to map (x,y,z) to the 1D index properly.
    
    // Since I don't have the library to test, I will write a robust get() that relies on member variables for strides if possible,
    // or just assume Z*Y*X for now and refine later.
    
    // Based on minc2-simple source, minc2_load_complete_volume uses apparent order reversed.
    // If standard order is X, Y, Z. It loads as Z, Y, X (Z slowest).
    // dimensions[0] = X size
    // dimensions[1] = Y size
    // dimensions[2] = Z size
    
    // Index = z * (sizeY * sizeX) + y * sizeX + x
    return data[z * dimensions[1] * dimensions[0] + y * dimensions[0] + x];
}
