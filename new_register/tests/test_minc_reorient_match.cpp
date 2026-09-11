/// test_minc_reorient_match.cpp — pins Volume::load()'s current MINC geometry and
/// voxel data against an independent reference computed directly from minc2-simple
/// calls (the same pattern Volume::load() used before the Task 3 fast-path refactor).
///
/// Run before the refactor: both sides use the old path, so this must already pass —
/// that is the point, it pins behaviour before VolumeReorient.{h,cpp} is introduced.
/// Run after: Volume::load() must still produce bit-identical geometry and data.

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <minc2-simple.h>

#include "Volume.h"

namespace
{

struct Reference
{
    glm::ivec3 dimensions{0, 0, 0};
    glm::dvec3 step{0.0};
    glm::dvec3 start{0.0};
    glm::dmat3 dirCos{1.0};
    std::vector<float> data;
};

Reference loadReference(const std::string& filename)
{
    Reference ref;

    minc2_file_handle h = nullptr;
    if (minc2_allocate(&h) != MINC2_SUCCESS)
        throw std::runtime_error("minc2_allocate failed: " + filename);

    if (minc2_open(h, filename.c_str()) != MINC2_SUCCESS)
    {
        minc2_free(h);
        throw std::runtime_error("minc2_open failed: " + filename);
    }

    if (minc2_setup_standard_order(h) != MINC2_SUCCESS)
    {
        minc2_close(h);
        minc2_free(h);
        throw std::runtime_error("minc2_setup_standard_order failed: " + filename);
    }

    int ndim = 0;
    if (minc2_ndim(h, &ndim) != MINC2_SUCCESS)
    {
        minc2_close(h);
        minc2_free(h);
        throw std::runtime_error("minc2_ndim failed: " + filename);
    }

    struct minc2_dimension *dims = nullptr;
    if (minc2_get_representation_dimensions(h, &dims) != MINC2_SUCCESS)
    {
        minc2_close(h);
        minc2_free(h);
        throw std::runtime_error("minc2_get_representation_dimensions failed: " + filename);
    }

    glm::ivec3 dim_indices{-1, -1, -1};
    for (int i = 0; i < ndim; ++i)
    {
        if (dims[i].id == MINC2_DIM_X) dim_indices[0] = i;
        else if (dims[i].id == MINC2_DIM_Y) dim_indices[1] = i;
        else if (dims[i].id == MINC2_DIM_Z) dim_indices[2] = i;
    }
    if (dim_indices[0] == -1 || dim_indices[1] == -1 || dim_indices[2] == -1)
    {
        minc2_close(h);
        minc2_free(h);
        throw std::runtime_error("Could not find X, Y, and Z dimensions: " + filename);
    }

    for (int axis = 0; axis < 3; ++axis)
    {
        int di = dim_indices[axis];
        ref.dimensions[axis] = dims[di].length;
        ref.step[axis]       = dims[di].step;
        ref.start[axis]      = dims[di].start;

        if (dims[di].have_dir_cos)
        {
            ref.dirCos[axis][0] = dims[di].dir_cos[0];
            ref.dirCos[axis][1] = dims[di].dir_cos[1];
            ref.dirCos[axis][2] = dims[di].dir_cos[2];
        }
        else
        {
            ref.dirCos[axis][0] = (axis == 0) ? 1.0 : 0.0;
            ref.dirCos[axis][1] = (axis == 1) ? 1.0 : 0.0;
            ref.dirCos[axis][2] = (axis == 2) ? 1.0 : 0.0;
        }
    }

    size_t total = 1;
    for (int i = 0; i < ndim; ++i)
        total *= static_cast<size_t>(dims[i].length);

    ref.data.resize(total);
    if (minc2_load_complete_volume(h, ref.data.data(), MINC2_FLOAT) != MINC2_SUCCESS)
    {
        minc2_close(h);
        minc2_free(h);
        throw std::runtime_error("minc2_load_complete_volume failed: " + filename);
    }

    minc2_close(h);
    minc2_free(h);
    return ref;
}

bool checkFile(const std::string& path)
{
    Reference ref = loadReference(path);

    Volume vol;
    vol.load(path);

    if (vol.dimensions != ref.dimensions)
    {
        std::cerr << path << ": dimensions mismatch: vol=("
                  << vol.dimensions.x << "," << vol.dimensions.y << "," << vol.dimensions.z
                  << ") ref=(" << ref.dimensions.x << "," << ref.dimensions.y << ","
                  << ref.dimensions.z << ")\n";
        return false;
    }

    const double eps = 1e-9;
    for (int i = 0; i < 3; ++i)
    {
        if (std::abs(vol.step[i] - ref.step[i]) > eps)
        {
            std::cerr << path << ": step[" << i << "] mismatch: vol=" << vol.step[i]
                      << " ref=" << ref.step[i] << "\n";
            return false;
        }
        if (std::abs(vol.start[i] - ref.start[i]) > eps)
        {
            std::cerr << path << ": start[" << i << "] mismatch: vol=" << vol.start[i]
                      << " ref=" << ref.start[i] << "\n";
            return false;
        }
        for (int j = 0; j < 3; ++j)
        {
            if (std::abs(vol.dirCos[i][j] - ref.dirCos[i][j]) > eps)
            {
                std::cerr << path << ": dirCos[" << i << "][" << j << "] mismatch: vol="
                          << vol.dirCos[i][j] << " ref=" << ref.dirCos[i][j] << "\n";
                return false;
            }
        }
    }

    if (vol.data.size() != ref.data.size())
    {
        std::cerr << path << ": data size mismatch: vol=" << vol.data.size()
                  << " ref=" << ref.data.size() << "\n";
        return false;
    }

    if (std::memcmp(vol.data.data(), ref.data.data(), vol.data.size() * sizeof(float)) != 0)
    {
        for (size_t i = 0; i < vol.data.size(); ++i)
        {
            if (std::memcmp(&vol.data[i], &ref.data[i], sizeof(float)) != 0)
            {
                std::cerr << path << ": data mismatch at voxel " << i << ": vol=" << vol.data[i]
                          << " ref=" << ref.data[i] << "\n";
                break;
            }
        }
        return false;
    }

    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <file1.mnc> [file2.mnc ...]\n";
        return 1;
    }

    bool allOk = true;
    for (int i = 1; i < argc; ++i)
        allOk = checkFile(argv[i]) && allOk;

    if (!allOk)
    {
        std::cerr << "MincReorientMatchTest: FAILED\n";
        return 1;
    }

    std::cout << "MincReorientMatchTest: all files matched\n";
    return 0;
}
