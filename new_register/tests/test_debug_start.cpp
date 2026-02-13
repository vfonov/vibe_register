#include "Volume.h"
#include <iostream>

int main() {
    Volume vol;
    try {
        vol.load("test_data/mni_icbm152_t1_tal_nlin_sym_09c.mnc");
    } catch (const std::exception& e) {
        std::cerr << "Failed: " << e.what() << "\n";
        return 1;
    }
    
    std::cout << "Volume dimensions: " << vol.dimensions[0] << " x " << vol.dimensions[1] << " x " << vol.dimensions[2] << "\n";
    std::cout << "Volume step: " << vol.step[0] << " x " << vol.step[1] << " x " << vol.step[2] << " mm\n";
    std::cout << "Volume start: " << vol.start[0] << " x " << vol.start[1] << " x " << vol.start[2] << " mm\n";
    
    // What should the center voxel be?
    // If dimensions are 193, the center is at index 96 (0-indexed)
    // Voxel 0 is at start, voxel 1 is at start + step, etc.
    // Voxel 96 is at start + 96 * step
    
    std::cout << "\nExpected center world positions:\n";
    std::cout << "  X: " << vol.start[0] + 96 * vol.step[0] << "\n";
    std::cout << "  Y: " << vol.start[1] + 114 * vol.step[1] << "\n";
    std::cout << "  Z: " << vol.start[2] + 96 * vol.step[2] << "\n";
    
    return 0;
}
