#include "Volume.h"
#include <iostream>
#include <cmath>
#include <glm/glm.hpp>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <volume_path>\n";
        return 1;
    }
    Volume vol;
    try {
        vol.load(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load volume: " << e.what() << "\n";
        return 1;
    }

    double sumX = 0, sumY = 0, sumZ = 0, total = 0;
    
    for (int z = 0; z < vol.dimensions.z; ++z) {
        for (int y = 0; y < vol.dimensions.y; ++y) {
            for (int x = 0; x < vol.dimensions.x; ++x) {
                float val = vol.data[z * vol.dimensions.y * vol.dimensions.x + y * vol.dimensions.x + x];
                if (val > 0) {
                    glm::dvec3 world;
                    glm::ivec3 voxel(x, y, z);
                    vol.transformVoxelToWorld(voxel, world);
                    
                    sumX += world.x * val;
                    sumY += world.y * val;
                    sumZ += world.z * val;
                    total += val;
                }
            }
        }
    }
    
    if (total > 0) {
        double comX = sumX / total;
        double comY = sumY / total;
        double comZ = sumZ / total;
        
        std::cout << "Center of mass for thick_slices volume:\n";
        std::cout << "  World coordinates: (" << comX << ", " << comY << ", " << comZ << ") mm\n";
        std::cout << "  Expected: (-0.03329536374, -19.1976859, 2.085919579) mm\n";
        
        bool x_ok = std::abs(comX - (-0.03329536374)) <= 0.1;
        bool y_ok = std::abs(comY - (-19.1976859)) <= 0.1;
        bool z_ok = std::abs(comZ - 2.085919579) <= 0.1;
        
        std::cout << "Match: " << (x_ok && y_ok && z_ok ? "YES" : "NO") << "\n";
        if (!x_ok) std::cout << "  X diff: " << (comX - (-0.03329536374)) << "\n";
        if (!y_ok) std::cout << "  Y diff: " << (comY - (-19.1976859)) << "\n";
        if (!z_ok) std::cout << "  Z diff: " << (comZ - 2.085919579) << "\n";
        
        return (x_ok && y_ok && z_ok) ? 0 : 1;
    }
    
    return 1;
}
