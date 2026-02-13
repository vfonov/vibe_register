#include "Volume.h"
#include <cmath>
#include <iostream>
#include <vector>
#include <algorithm>
#include <limits>

// Include minc2-simple header
#include <minc2-simple.h>

Volume::Volume()
{
    dimensions[0] = 0;
    dimensions[1] = 0;
    dimensions[2] = 0;
}

Volume::~Volume() {}

void Volume::generate_test_data()
{
    dimensions[0] = 256;
    dimensions[1] = 256;
    dimensions[2] = 256;

    step[0] = 1.0;
    step[1] = 1.0;
    step[2] = 1.0;

    start[0] = -128.0;
    start[1] = -128.0;
    start[2] = -128.0;

    dirCos[0][0] = 1.0; dirCos[0][1] = 0.0; dirCos[0][2] = 0.0;
    dirCos[1][0] = 0.0; dirCos[1][1] = 1.0; dirCos[1][2] = 0.0;
    dirCos[2][0] = 0.0; dirCos[2][1] = 0.0; dirCos[2][2] = 1.0;

    data.resize(256 * 256 * 256);

    min_value = 0.0f;
    max_value = 1.0f;

    for (int z = 0; z < 256; ++z)
    {
        for (int y = 0; y < 256; ++y)
        {
            for (int x = 0; x < 256; ++x)
            {
                float val = static_cast<float>(x) / 256.0f;
                // Grid lines
                if (z % 32 == 0 || y % 32 == 0 || x % 32 == 0)
                    val = 0.8f;

                // Sphere in center
                float dx = x - 128.0f;
                float dy = y - 128.0f;
                float dz = z - 128.0f;
                if (std::sqrt(dx * dx + dy * dy + dz * dz) < 60.0f)
                    val = 1.0f;

                data[z * 256 * 256 + y * 256 + x] = val;
            }
        }
    }
    std::cerr << "Generated synthetic volume 256x256x256" << std::endl;
}

bool Volume::load(const std::string& filename)
{
    if (filename.empty())
    {
        std::cerr << "Volume::load: Empty filename provided." << std::endl;
        return false;
    }

    minc2_file_handle h;
    if (minc2_allocate(&h) != MINC2_SUCCESS)
    {
        std::cerr << "Volume::load: Failed to allocate minc2 handle." << std::endl;
        return false;
    }

    if (minc2_open(h, filename.c_str()) != MINC2_SUCCESS)
    {
        std::cerr << "Volume::load: Failed to open file: " << filename << std::endl;
        minc2_free(h);
        return false;
    }

    if (minc2_setup_standard_order(h) != MINC2_SUCCESS)
    {
        std::cerr << "Volume::load: Failed to setup standard dimension order." << std::endl;
        minc2_close(h);
        minc2_free(h);
        return false;
    }

    int ndim = 0;
    if (minc2_ndim(h, &ndim) != MINC2_SUCCESS)
    {
        std::cerr << "Volume::load: Failed to get number of dimensions." << std::endl;
        minc2_close(h);
        minc2_free(h);
        return false;
    }

    struct minc2_dimension *dims = nullptr;
    if (minc2_get_representation_dimensions(h, &dims) != MINC2_SUCCESS)
    {
        std::cerr << "Volume::load: Failed to get dimension info." << std::endl;
        minc2_close(h);
        minc2_free(h);
        return false;
    }

    // Find X, Y, Z dimension indices
    int dim_indices[3] = { -1, -1, -1 };

    for (int i = 0; i < ndim; ++i)
    {
        if (dims[i].id == MINC2_DIM_X) dim_indices[0] = i;
        else if (dims[i].id == MINC2_DIM_Y) dim_indices[1] = i;
        else if (dims[i].id == MINC2_DIM_Z) dim_indices[2] = i;
    }

    if (dim_indices[0] == -1 || dim_indices[1] == -1 || dim_indices[2] == -1)
    {
        std::cerr << "Volume::load: Could not find X, Y, and Z dimensions." << std::endl;
        minc2_close(h);
        minc2_free(h);
        return false;
    }

    // Extract dimension sizes and spatial metadata
    for (int axis = 0; axis < 3; ++axis)
    {
        int di = dim_indices[axis];
        dimensions[axis] = dims[di].length;
        step[axis]       = dims[di].step;
        start[axis]      = dims[di].start;

        if (dims[di].have_dir_cos)
        {
            dirCos[axis][0] = dims[di].dir_cos[0];
            dirCos[axis][1] = dims[di].dir_cos[1];
            dirCos[axis][2] = dims[di].dir_cos[2];
        }
        else
        {
            // Default direction cosines (identity)
            dirCos[axis][0] = (axis == 0) ? 1.0 : 0.0;
            dirCos[axis][1] = (axis == 1) ? 1.0 : 0.0;
            dirCos[axis][2] = (axis == 2) ? 1.0 : 0.0;
        }
    }

    size_t total_voxels = 1;
    for (int i = 0; i < ndim; ++i)
    {
        total_voxels *= dims[i].length;
    }

    if (total_voxels == 0)
    {
        std::cerr << "Volume::load: Volume has 0 voxels." << std::endl;
        minc2_close(h);
        minc2_free(h);
        return false;
    }

    try
    {
        data.resize(total_voxels);
    }
    catch (const std::bad_alloc& e)
    {
        std::cerr << "Volume::load: Failed to allocate memory for volume data ("
                  << total_voxels * sizeof(float) << " bytes)." << std::endl;
        minc2_close(h);
        minc2_free(h);
        return false;
    }

    if (minc2_load_complete_volume(h, data.data(), MINC2_FLOAT) != MINC2_SUCCESS)
    {
        std::cerr << "Volume::load: Failed to load volume data." << std::endl;
        minc2_close(h);
        minc2_free(h);
        return false;
    }

    // Calculate min/max for visualization
    min_value = std::numeric_limits<float>::max();
    max_value = std::numeric_limits<float>::lowest();

    for (float v : data)
    {
        if (v < min_value) min_value = v;
        if (v > max_value) max_value = v;
    }

    if (min_value >= max_value)
    {
        max_value = min_value + 1.0f;
    }

    std::cerr << "Loaded volume: " << filename << "\n";
    std::cerr << "  Dimensions: " << dimensions[0] << " x "
              << dimensions[1] << " x " << dimensions[2] << "\n";
    std::cerr << "  Step:  " << step[0] << " x " << step[1] << " x " << step[2] << " mm\n";
    std::cerr << "  Start: " << start[0] << ", " << start[1] << ", " << start[2] << " mm\n";
    std::cerr << "  Range: [" << min_value << ", " << max_value << "]\n";

    minc2_close(h);
    minc2_free(h);

    return true;
}

float Volume::get(int x, int y, int z) const
{
    if (x < 0 || x >= dimensions[0] ||
        y < 0 || y >= dimensions[1] ||
        z < 0 || z >= dimensions[2]) return 0.0f;

    return data[z * dimensions[1] * dimensions[0] + y * dimensions[0] + x];
}

void Volume::worldExtent(double extent[3]) const
{
    for (int i = 0; i < 3; ++i)
    {
        extent[i] = std::abs(step[i]) * dimensions[i];
    }
}

double Volume::slicePixelAspect(int axisU, int axisV) const
{
    double su = std::abs(step[axisU]);
    double sv = std::abs(step[axisV]);
    if (sv < 1e-12) return 1.0;
    return su / sv;
}