#include "Volume.h"
#include <iostream>
#include <cmath>

int main() {
    Volume vol;
    try {
        vol.load("/app/test_data/mni_icbm152_t1_tal_nlin_sym_09c.mnc");
    } catch (const std::exception& e) {
        std::cerr << "Failed to load volume: " << e.what() << "\n";
        return 1;
    }

    // Calculate center of mass from data using the Volume's matrix transformation
    double sumX = 0, sumY = 0, sumZ = 0, total = 0;
    
    for (int z = 0; z < vol.dimensions[2]; ++z) {
        for (int y = 0; y < vol.dimensions[1]; ++y) {
            for (int x = 0; x < vol.dimensions[0]; ++x) {
                float val = vol.data[z * vol.dimensions[1] * vol.dimensions[0] + y * vol.dimensions[0] + x];
                if (val > 0) {
                    // Use the Volume class's transformVoxelToWorld method
                    int voxel[3] = { x, y, z };
                    double world[3];
                    vol.transformVoxelToWorld(voxel, world);
                    
                    sumX += world[0] * val;
                    sumY += world[1] * val;
                    sumZ += world[2] * val;
                    total += val;
                }
            }
        }
    }
    
    if (total > 0) {
        double comX = sumX / total;
        double comY = sumY / total;
        double comZ = sumZ / total;
        
        std::cout << "Center of mass from data:\n";
        std::cout << "  World coordinates: (" << comX << ", " << comY << ", " << comZ << ") mm\n";
        std::cout << "  Expected: (0, -19.19922251, 2.143570161) mm\n";
        
        // Check if close to expected (within 1mm tolerance)
        bool x_ok = std::abs(comX - 0.0) < 1.0;
        bool y_ok = std::abs(comY - (-19.19922251)) < 1.0;
        bool z_ok = std::abs(comZ - 2.143570161) < 1.0;
        
        std::cout << "Match: " << (x_ok && y_ok && z_ok ? "YES" : "NO") << "\n";
        if (!x_ok) std::cout << "  X diff: " << (comX - 0.0) << "\n";
        if (!y_ok) std::cout << "  Y diff: " << (comY - (-19.19922251)) << "\n";
        if (!z_ok) std::cout << "  Z diff: " << (comZ - 2.143570161) << "\n";
        
        return (x_ok && y_ok && z_ok) ? 0 : 1;
    }
    
    return 1;
}
