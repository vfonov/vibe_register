#include "Volume.h"
#include <cmath>
#include <vector>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <sstream>
#include <iostream>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

// Include minc2-simple header
#include <minc2-simple.h>

// RAII wrapper for minc2 file handles — ensures close+free on scope exit.
class Minc2Handle
{
public:
    Minc2Handle()
    {
        if (minc2_allocate(&h_) != MINC2_SUCCESS)
            throw std::runtime_error("Failed to allocate minc2 handle");
    }

    ~Minc2Handle()
    {
        if (opened_) minc2_close(h_);
        minc2_free(h_);
    }

    void open(const std::string& filename)
    {
        if (minc2_open(h_, filename.c_str()) != MINC2_SUCCESS)
            throw std::runtime_error("Failed to open file: " + filename);
        opened_ = true;
    }

    minc2_file_handle get() const { return h_; }

    Minc2Handle(const Minc2Handle&) = delete;
    Minc2Handle& operator=(const Minc2Handle&) = delete;
    Minc2Handle(Minc2Handle&& other) noexcept : h_(other.h_), opened_(other.opened_) {
        other.h_ = nullptr;
        other.opened_ = false;
    }
    Minc2Handle& operator=(Minc2Handle&& other) noexcept {
        if (this != &other) {
            if (opened_) minc2_close(h_);
            minc2_free(h_);
            h_ = other.h_;
            opened_ = other.opened_;
            other.h_ = nullptr;
            other.opened_ = false;
        }
        return *this;
    }

private:
    minc2_file_handle h_ = nullptr;
    bool opened_ = false;
};

Volume::Volume()
{
    dimensions = glm::ivec3(0, 0, 0);
}

Volume::~Volume() {}

Volume::Volume(Volume&& other) noexcept
    : dimensions(other.dimensions),
      step(other.step),
      start(other.start),
      dirCos(other.dirCos),
      data(std::move(other.data)),
      min_value(other.min_value),
      max_value(other.max_value),
      voxelToWorld(other.voxelToWorld),
      worldToVoxel(other.worldToVoxel),
      tags(std::move(other.tags))
{
    other.dimensions = glm::ivec3(0, 0, 0);
    other.min_value = 0.0f;
    other.max_value = 1.0f;
}

Volume& Volume::operator=(Volume&& other) noexcept {
    if (this != &other) {
        dimensions = other.dimensions;
        step = other.step;
        start = other.start;
        dirCos = other.dirCos;
        data = std::move(other.data);
        min_value = other.min_value;
        max_value = other.max_value;
        voxelToWorld = other.voxelToWorld;
        worldToVoxel = other.worldToVoxel;
        tags = std::move(other.tags);
        
        other.dimensions = glm::ivec3(0, 0, 0);
        other.min_value = 0.0f;
        other.max_value = 1.0f;
    }
    return *this;
}

void Volume::generate_test_data()
{
    dimensions = glm::ivec3(256, 256, 256);

    step = glm::dvec3(1.0, 1.0, 1.0);

    start = glm::dvec3(-128.0, -128.0, -128.0);

    dirCos = glm::dmat3(1.0, 0.0, 0.0,
                        0.0, 1.0, 0.0,
                        0.0, 0.0, 1.0);

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

}

void Volume::load(const std::string& filename)
{
    if (filename.empty())
        throw std::runtime_error("Empty filename provided");

    Minc2Handle h;
    h.open(filename);

    if (minc2_setup_standard_order(h.get()) != MINC2_SUCCESS)
        throw std::runtime_error("Failed to setup standard dimension order: " + filename);

    int ndim = 0;
    if (minc2_ndim(h.get(), &ndim) != MINC2_SUCCESS)
        throw std::runtime_error("Failed to get number of dimensions: " + filename);

    struct minc2_dimension *dims = nullptr;
    if (minc2_get_representation_dimensions(h.get(), &dims) != MINC2_SUCCESS)
        throw std::runtime_error("Failed to get dimension info: " + filename);

    // Find X, Y, Z dimension indices
    glm::ivec3 dim_indices{-1, -1, -1};

    for (int i = 0; i < ndim; ++i)
    {
        if (dims[i].id == MINC2_DIM_X) dim_indices[0] = i;
        else if (dims[i].id == MINC2_DIM_Y) dim_indices[1] = i;
        else if (dims[i].id == MINC2_DIM_Z) dim_indices[2] = i;
    }

    if (dim_indices[0] == -1 || dim_indices[1] == -1 || dim_indices[2] == -1)
        throw std::runtime_error("Could not find X, Y, and Z dimensions: " + filename);

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

    // Build voxel-to-world transformation matrix
    // MINC: voxel i corner is at start + i * step * dirCos
    // For 4x4 matrix: world = affine * voxel + start
    // where affine = dirCos * diag(step)
    
    glm::dmat3 dirCos3(dirCos[0][0], dirCos[0][1], dirCos[0][2],
                       dirCos[1][0], dirCos[1][1], dirCos[1][2],
                       dirCos[2][0], dirCos[2][1], dirCos[2][2]);
    
    glm::dvec3 scale(step.x, step.y, step.z);
    glm::dvec3 trans(start.x, start.y, start.z);
    
    // dirCos * diag(scale) = each column scaled by scale
    glm::dmat3 affine = dirCos3;
    for (int i = 0; i < 3; ++i)
        affine[i] *= scale;
    
    // For 4x4 matrix: world = affine * voxel + start
    voxelToWorld = glm::dmat4(
        glm::dvec4(affine[0], 0.0),
        glm::dvec4(affine[1], 0.0),
        glm::dvec4(affine[2], 0.0),
        glm::dvec4(trans, 1.0)
    );
    
    // Compute inverse for world-to-voxel
    // world = affine * voxel + start
    // world - start = affine * voxel
    // voxel = affine^-1 * (world - start)
    worldToVoxel = glm::inverse(voxelToWorld);

    size_t total_voxels = 1;
    for (int i = 0; i < ndim; ++i)
    {
        total_voxels *= dims[i].length;
    }

    if (total_voxels == 0)
        throw std::runtime_error("Volume has 0 voxels: " + filename);

    data.resize(total_voxels);  // std::bad_alloc propagates naturally

    if (minc2_load_complete_volume(h.get(), data.data(), MINC2_FLOAT) != MINC2_SUCCESS)
        throw std::runtime_error("Failed to load volume data: " + filename);

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
}

float Volume::get(int x, int y, int z) const
{
    if (x < 0 || x >= dimensions.x ||
        y < 0 || y >= dimensions.y ||
        z < 0 || z >= dimensions.z) return 0.0f;

    return data[z * dimensions.y * dimensions.x + y * dimensions.x + x];
}

void Volume::worldExtent(glm::dvec3& extent) const
{
    extent.x = std::abs(step.x) * dimensions.x;
    extent.y = std::abs(step.y) * dimensions.y;
    extent.z = std::abs(step.z) * dimensions.z;
}

double Volume::slicePixelAspect(int axisU, int axisV) const
{
    double su = std::abs(step[axisU]);
    double sv = std::abs(step[axisV]);
    if (sv < 1e-12) return 1.0;
    return su / sv;
}

void Volume::transformVoxelToWorld(const glm::ivec3& voxel, glm::dvec3& world) const
{
    glm::dvec4 v(static_cast<double>(voxel.x), static_cast<double>(voxel.y), static_cast<double>(voxel.z), 1.0);
    glm::dvec4 w = voxelToWorld * v;
    world.x = w.x;
    world.y = w.y;
    world.z = w.z;
}

void Volume::transformWorldToVoxel(const glm::dvec3& world, glm::ivec3& voxel) const
{
    glm::dvec4 w(world.x, world.y, world.z, 1.0);
    glm::dvec4 v = worldToVoxel * w;
    voxel.x = static_cast<int>(std::round(v.x));
    voxel.y = static_cast<int>(std::round(v.y));
    voxel.z = static_cast<int>(std::round(v.z));
    
    voxel.x = std::clamp(voxel.x, 0, dimensions.x - 1);
    voxel.y = std::clamp(voxel.y, 0, dimensions.y - 1);
    voxel.z = std::clamp(voxel.z, 0, dimensions.z - 1);
}

void Volume::loadTags(const std::string& path) {
    tags.load(path);
}

void Volume::saveTags(const std::string& path) {
    tags.save(path);
}

void Volume::clearTags() {
    tags.clear();
}
