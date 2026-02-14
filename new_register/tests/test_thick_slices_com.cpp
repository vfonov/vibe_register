#include "Volume.h"
#include <iostream>
#include <cmath>

int main() {
    Volume vol;
    try {
        vol.load("/app/test_data/mni_icbm152_t1_tal_nlin_sym_09c_thick_slices.mnc");
    } catch (const std::exception& e) {
        std::cerr << "Failed to load volume: " << e.what() << "\n";
        return 1;
    }

    double sumX = 0, sumY = 0, sumZ = 0, total = 0;
    
    for (int z = 0; z < vol.dimensions[2]; ++z) {
        for (int y = 0; y < vol.dimensions[1]; ++y) {
            for (int x = 0; x < vol.dimensions[0]; ++x) {
                float val = vol.data[z * vol.dimensions[1] * vol.dimensions[0] + y * vol.dimensions[0] + x];
                if (val > 0) {
                    double world[3];
                    int voxel[3] = {x, y, z};
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
