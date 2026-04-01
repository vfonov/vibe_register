/// dump_vol.cpp — print MINC volume metadata for debugging.
///
/// Usage: dump_vol <file.mnc> [file2.mnc ...]
///
/// Prints for each volume:
///   - Dimensions, voxel step, world start
///   - Direction cosines (rows = X/Y/Z axes)
///   - voxelToWorld matrix (column-major display: each row is one world axis)
///   - World coordinates of corner voxels (0,0,0) and (max,max,max)

#include <cstdlib>
#include <iostream>
#include <iomanip>

#include "Volume.h"

static void dumpVolume(const std::string& path)
{
    Volume v;
    try { v.load(path); }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR loading " << path << ": " << e.what() << "\n";
        return;
    }

    std::cerr << std::fixed << std::setprecision(6);
    std::cerr << "=== " << path << " ===\n";
    std::cerr << "  dims  : " << v.dimensions.x << " x " << v.dimensions.y << " x " << v.dimensions.z << "\n";
    std::cerr << "  step  : " << v.step.x << "  " << v.step.y << "  " << v.step.z << "\n";
    std::cerr << "  start : " << v.start.x << "  " << v.start.y << "  " << v.start.z << "\n";
    std::cerr << "  range : [" << v.min_value << ", " << v.max_value << "]\n";

    std::cerr << "  dirCos X: "
              << v.dirCos[0][0] << "  " << v.dirCos[0][1] << "  " << v.dirCos[0][2] << "\n";
    std::cerr << "  dirCos Y: "
              << v.dirCos[1][0] << "  " << v.dirCos[1][1] << "  " << v.dirCos[1][2] << "\n";
    std::cerr << "  dirCos Z: "
              << v.dirCos[2][0] << "  " << v.dirCos[2][1] << "  " << v.dirCos[2][2] << "\n";

    // Print voxelToWorld as 4 rows (world X,Y,Z,W) x 4 cols (vox x,y,z,1)
    std::cerr << "  voxelToWorld (row=world axis, col=vox axis):\n";
    for (int row = 0; row < 4; ++row)
    {
        std::cerr << "    [";
        for (int col = 0; col < 4; ++col)
            std::cerr << std::setw(12) << v.voxelToWorld[col][row] << (col < 3 ? "  " : "");
        std::cerr << " ]\n";
    }

    auto w0 = v.voxelToWorld * glm::dvec4(0, 0, 0, 1);
    auto wm = v.voxelToWorld * glm::dvec4(v.dimensions.x - 1,
                                           v.dimensions.y - 1,
                                           v.dimensions.z - 1, 1);
    std::cerr << "  world(0,0,0)      : " << w0.x << "  " << w0.y << "  " << w0.z << "\n";
    std::cerr << "  world(max,max,max): " << wm.x << "  " << wm.y << "  " << wm.z << "\n";
    std::cerr << "\n";
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: dump_vol <file.mnc> [file2.mnc ...]\n";
        return 1;
    }
    for (int i = 1; i < argc; ++i)
        dumpVolume(argv[i]);
    return 0;
}
