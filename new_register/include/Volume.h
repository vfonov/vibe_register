#pragma once

#include <vector>
#include <string>
#include <array>
#include <stdexcept>

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
};
