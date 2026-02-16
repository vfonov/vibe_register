#pragma once

#include <vector>
#include <string>
#include <array>
#include <stdexcept>

#include <glm/glm.hpp>

#include "TagWrapper.hpp"

class Volume {
public:
    glm::ivec3 dimensions{0, 0, 0};  // X, Y, Z voxel counts

    /// Voxel spacing in mm along each axis (always positive after
    /// setup_standard_order).  Index 0 = X, 1 = Y, 2 = Z.
    glm::dvec3 step{1.0, 1.0, 1.0};

    /// World coordinate of the first voxel along each axis.
    glm::dvec3 start{0.0, 0.0, 0.0};

    /// Direction cosines per axis (unit vectors in world space).
    /// dirCos[i] is a 3-element vector for axis i.
    glm::dmat3 dirCos{1.0, 0.0, 0.0,
                      0.0, 1.0, 0.0,
                      0.0, 0.0, 1.0};

    std::vector<float> data;
    float min_value = 0.0f;
    float max_value = 1.0f;

    /// 4x4 transformation matrix from voxel coordinates to world coordinates.
    /// The voxel (i,j,k) is centered, so position = start + (i+0.5, j+0.5, k+0.5).
    glm::dmat4 voxelToWorld{1.0};  // Identity by default

    /// Inverse matrix: world coordinates to voxel coordinates.
    glm::dmat4 worldToVoxel{1.0};  // Identity by default

    Volume();
    ~Volume();

    Volume(Volume&& other) noexcept;
    Volume& operator=(Volume&& other) noexcept;

    /// Load a MINC2 volume from disk.
    /// @throws std::runtime_error on any failure (file not found, bad format, etc.)
    void load(const std::string& filename);
    float get(int x, int y, int z) const;
    void generate_test_data();

    /// Return the physical (world-space) size of the volume along each
    /// axis:  |step[i]| * dimensions[i].
    void worldExtent(glm::dvec3& extent) const;

    /// Compute the pixel aspect ratio for a 2D slice view whose two
    /// in-plane axes are axisU and axisV.  Returns |step[axisU]| /
    /// |step[axisV]|, i.e. the width of one pixel relative to its height.
    double slicePixelAspect(int axisU, int axisV) const;
    
    /// Transform voxel coordinates (integers) to world coordinates using the
    /// precomputed voxel-to-world matrix.
    /// Both voxel and world use MINC order: .x = X, .y = Y, .z = Z.
    void transformVoxelToWorld(const glm::ivec3& voxel, glm::dvec3& world) const;
    
    /// Transform world coordinates to voxel indices using the precomputed
    /// world-to-voxel matrix. Result is rounded to nearest integer and clamped.
    /// Both world and voxel use MINC order: .x = X, .y = Y, .z = Z.
    void transformWorldToVoxel(const glm::dvec3& world, glm::ivec3& voxel) const;

    /// Tag management methods
    void loadTags(const std::string& path);
    void saveTags(const std::string& path);
    void clearTags();
    const std::vector<glm::vec3>& getTagPoints() const { return tags.points(); }
    const std::vector<std::string>& getTagLabels() const { return tags.labels(); }
    int getTagCount() const { return tags.tagCount(); }
    bool hasTags() const { return tags.hasTags(); }

    TagWrapper tags;
};
