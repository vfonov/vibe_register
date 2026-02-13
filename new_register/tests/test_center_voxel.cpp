#include "Volume.h"
#include <iostream>

int main() {
    Volume vol;
    try {
        vol.load("test_data/mni_icbm152_t1_tal_nlin_sym_09c.mnc");
    } catch (const std::exception& e) {
        return 1;
    }
    
    std::cout << "Dimensions: " << vol.dimensions[0] << " x " << vol.dimensions[1] << " x " << vol.dimensions[2] << "\n";
    std::cout << "Step: " << vol.step[0] << " x " << vol.step[1] << " x " << vol.step[2] << "\n";
    std::cout << "Start: " << vol.start[0] << " x " << vol.start[1] << " x " << vol.start[2] << "\n";
    
    // For 193 voxels of 1mm each, the span is 192mm
    // If voxel 0 is at start, voxel 192 is at start + 192
    // The center of the volume is at (start + start+192) / 2 = start + 96
    
    // So voxel 96 should be at the center of the volume
    // But wait - MINC says voxel i is at start + (i + 0.5) * step
    // So voxel 0 is at start + 0.5
    // And voxel 192 would be at start + 192.5
    // The center would be at start + 96.5
    
    int centerVoxel = vol.dimensions[0] / 2;  // 193/2 = 96
    
    std::cout << "\nCenter voxel index: " << centerVoxel << "\n";
    std::cout << "Voxel 0 position (MINC spec): start + 0.5*step = " << (vol.start[0] + 0.5) << "\n";
    std::cout << "Voxel " << centerVoxel << " position: start + (" << centerVoxel << "+0.5)*step = " << (vol.start[0] + (centerVoxel + 0.5)) << "\n";
    std::cout << "Volume span: " << (vol.dimensions[0] - 1) * vol.step[0] << " mm\n";
    std::cout << "Volume center (min+max)/2: (" << vol.start[0] + 0.5 << " + " << (vol.start[0] + (vol.dimensions[0]-1 + 0.5)) << ") / 2 = " << (vol.start[0] + 0.5 + vol.dimensions[0]-1)/2.0 << "\n";
    
    return 0;
}
