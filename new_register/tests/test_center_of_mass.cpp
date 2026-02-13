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

    // Calculate center of mass from data
    double sumX = 0, sumY = 0, sumZ = 0, total = 0;
    
    for (int z = 0; z < vol.dimensions[2]; ++z) {
        for (int y = 0; y < vol.dimensions[1]; ++y) {
            for (int x = 0; x < vol.dimensions[0]; ++x) {
                float val = vol.data[z * vol.dimensions[1] * vol.dimensions[0] + y * vol.dimensions[0] + x];
                if (val > 0) {
                    // Voxel center in world coordinates
                    double wx = vol.start[0] + (x + 0.5) * vol.step[0];
                    double wy = vol.start[1] + (y + 0.5) * vol.step[1];
                    double wz = vol.start[2] + (z + 0.5) * vol.step[2];
                    
                    sumX += wx * val;
                    sumY += wy * val;
                    sumZ += wz * val;
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
        
        // Check if close to expected
        bool x_ok = std::abs(comX - 0.0) < 0.001;
        bool y_ok = std::abs(comY - (-19.19922251)) < 0.001;
        bool z_ok = std::abs(comZ - 2.143570161) < 0.001;
        
        std::cout << "Match: " << (x_ok && y_ok && z_ok ? "YES" : "NO") << "\n";
        if (!x_ok) std::cout << "  X diff: " << (comX - 0.0) << "\n";
        if (!y_ok) std::cout << "  Y diff: " << (comY - (-19.19922251)) << "\n";
        if (!z_ok) std::cout << "  Z diff: " << (comZ - 2.143570161) << "\n";
        
        return (x_ok && y_ok && z_ok) ? 0 : 1;
    }
    
    return 1;
}
