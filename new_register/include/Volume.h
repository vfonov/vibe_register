#pragma once

#include <vector>
#include <string>
#include <array>
#include <stdexcept>

#include <glm/glm.hpp>

class Volume {
public:
    int dimensions[3] = { 0, 0, 0 };  // X, Y, Z voxel counts

    /// Voxel spacing in mm along each axis (always positive after
    /// setup_standard_order).  Index 0 = X, 1 = Y, 2 = Z.
    double step[3] = { 1.0, 1.0, 1.0 };

    /// World coordinate of the first voxel along each axis.
    double start[3] = { 0.0, 0.0, 0.0 };

    /// Direction cosines per axis (unit vectors in world space).
    /// dirCos[i] is a 3-element vector for axis i.
    double dirCos[3][3] = {
        { 1.0, 0.0, 0.0 },   // X axis
        { 0.0, 1.0, 0.0 },   // Y axis
        { 0.0, 0.0, 1.0 }    // Z axis
    };

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

    /// Load a MINC2 volume from disk.
    /// @throws std::runtime_error on any failure (file not found, bad format, etc.)
    void load(const std::string& filename);
    float get(int x, int y, int z) const;
    void generate_test_data();

    /// Return the physical (world-space) size of the volume along each
    /// axis:  |step[i]| * dimensions[i].
    void worldExtent(double extent[3]) const;

    /// Compute the pixel aspect ratio for a 2D slice view whose two
    /// in-plane axes are axisU and axisV.  Returns |step[axisU]| /
    /// |step[axisV]|, i.e. the width of one pixel relative to its height.
    double slicePixelAspect(int axisU, int axisV) const;
    
    /// Transform voxel coordinates (integers) to world coordinates using the
    /// precomputed voxel-to-world matrix.
    /// Both voxel[] and world[] use MINC order: [0]=X, [1]=Y, [2]=Z.
    void transformVoxelToWorld(const int voxel[3], double world[3]) const;
    
    /// Transform world coordinates to voxel indices using the precomputed
    /// world-to-voxel matrix. Result is rounded to nearest integer and clamped.
    /// Both world[] and voxel[] use MINC order: [0]=X, [1]=Y, [2]=Z.
    void transformWorldToVoxel(const double world[3], int voxel[3]) const;
};
