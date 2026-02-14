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
    // The expected COM values use corner convention: world = start + voxel * step
    // We need to use the same convention for proper comparison
    
    double sumX = 0, sumY = 0, sumZ = 0, total = 0;
    
    for (int z = 0; z < vol.dimensions[2]; ++z) {
        for (int y = 0; y < vol.dimensions[1]; ++y) {
            for (int x = 0; x < vol.dimensions[0]; ++x) {
                float val = vol.data[z * vol.dimensions[1] * vol.dimensions[0] + y * vol.dimensions[0] + x];
                if (val > 0) {
                    // Use corner convention: world = start + voxel * step
                    // (same convention used to compute the expected COM values)
                    double world[3] = {
                        vol.start[0] + x * vol.step[0],
                        vol.start[1] + y * vol.step[1],
                        vol.start[2] + z * vol.step[2]
                    };
                    
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
        
        // Check if calculated COM matches expected values within 0.1mm tolerance
        bool x_ok = std::abs(comX - 0.0) <= 0.1;
        bool y_ok = std::abs(comY - (-19.19922251)) <= 0.1;
        bool z_ok = std::abs(comZ - 2.143570161) <= 0.1;
        
        std::cout << "Match: " << (x_ok && y_ok && z_ok ? "YES" : "NO") << "\n";
        if (!x_ok) std::cout << "  X diff: " << (comX - 0.0) << "\n";
        if (!y_ok) std::cout << "  Y diff: " << (comY - (-19.19922251)) << "\n";
        if (!z_ok) std::cout << "  Z diff: " << (comZ - 2.143570161) << "\n";
        
        // Return failure if COM doesn't match expected values within 0.1mm
        return (x_ok && y_ok && z_ok) ? 0 : 1;
    }
    
    return 1;
}
