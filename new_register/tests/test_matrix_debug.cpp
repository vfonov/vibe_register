#include "Volume.h"
#include <iostream>
#include <cmath>
#include <filesystem>

void printMatrix(const glm::dmat4& m, const char* name) {
    std::cout << name << ":\n";
    for (int i = 0; i < 4; ++i) {
        std::cout << "  [" << i << "] (" << m[i].x << ", " << m[i].y << ", " << m[i].z << ", " << m[i].w << ")\n";
    }
}

int main() {
    std::filesystem::path testDataDir = "/app/test_data";

    Volume hiRes, loRes;
    hiRes.load((testDataDir / "mni_icbm152_t1_tal_nlin_sym_09c.mnc").string());
    loRes.load((testDataDir / "mni_icbm152_t1_tal_nlin_sym_09c_thick_slices.mnc").string());
    
    std::cout << "=== Volume 1 (high-res) ===\n";
    std::cout << "dimensions (X,Y,Z): " << hiRes.dimensions[0] << ", " << hiRes.dimensions[1] << ", " << hiRes.dimensions[2] << "\n";
    std::cout << "step (X,Y,Z): " << hiRes.step[0] << ", " << hiRes.step[1] << ", " << hiRes.step[2] << "\n";
    std::cout << "start (X,Y,Z): " << hiRes.start[0] << ", " << hiRes.start[1] << ", " << hiRes.start[2] << "\n";
    printMatrix(hiRes.voxelToWorld, "voxelToWorld");
    printMatrix(hiRes.worldToVoxel, "worldToVoxel");
    
    std::cout << "\n=== Volume 2 (thick-slices) ===\n";
    std::cout << "dimensions (X,Y,Z): " << loRes.dimensions[0] << ", " << loRes.dimensions[1] << ", " << loRes.dimensions[2] << "\n";
    std::cout << "step (X,Y,Z): " << loRes.step[0] << ", " << loRes.step[1] << ", " << loRes.step[2] << "\n";
    std::cout << "start (X,Y,Z): " << loRes.start[0] << ", " << loRes.start[1] << ", " << loRes.start[2] << "\n";
    printMatrix(loRes.voxelToWorld, "voxelToWorld");
    printMatrix(loRes.worldToVoxel, "worldToVoxel");
    
    // Test: Verify round-trip
    std::cout << "\n=== Test round-trip: voxel -> world -> voxel ===\n";
    
    // For hiRes: test voxel (96, 114, 78) which should be center
    int testVoxel[3] = {96, 114, 78};  // X=96, Y=114, Z=78 in MINC convention
    double world[3];
    
    // Manual calculation: world = start + voxel * step
    double expectedWorld[3];
    expectedWorld[0] = hiRes.start[0] + testVoxel[0] * hiRes.step[0];
    expectedWorld[1] = hiRes.start[1] + testVoxel[1] * hiRes.step[1];
    expectedWorld[2] = hiRes.start[2] + testVoxel[2] * hiRes.step[2];
    std::cout << "Manual: voxel (" << testVoxel[0] << ", " << testVoxel[1] << ", " << testVoxel[2] << ") -> world ("
              << expectedWorld[0] << ", " << expectedWorld[1] << ", " << expectedWorld[2] << ")\n";
    
    // Using the function (MINC order: X, Y, Z)
    int mincVoxel[3] = {96, 114, 78};  // X=96, Y=114, Z=78 in MINC convention
    hiRes.transformVoxelToWorld(mincVoxel, world);
    std::cout << "transformVoxelToWorld: voxel (" << mincVoxel[0] << ", " << mincVoxel[1] << ", " << mincVoxel[2] << ") -> world ("
              << world[0] << ", " << world[1] << ", " << world[2] << ")\n";
    
    // Convert back
    int backVoxel[3];
    hiRes.transformWorldToVoxel(world, backVoxel);
    std::cout << "Round-trip: world -> voxel (" << backVoxel[0] << ", " << backVoxel[1] << ", " << backVoxel[2] << ")\n";
    
    // Test with world (0,0,0)
    std::cout << "\n=== Test: world (0,0,0) -> voxel ===\n";
    double zeroWorld[3] = {0, 0, 0};
    int zeroVoxel[3];
    hiRes.transformWorldToVoxel(zeroWorld, zeroVoxel);
    std::cout << "hiRes: world (0,0,0) -> voxel (" << zeroVoxel[0] << ", " << zeroVoxel[1] << ", " << zeroVoxel[2] << ")\n";
    std::cout << "Expected: (96, 132, 78) = (X, Y, Z)\n";
    
    loRes.transformWorldToVoxel(zeroWorld, zeroVoxel);
    std::cout << "loRes: world (0,0,0) -> voxel (" << zeroVoxel[0] << ", " << zeroVoxel[1] << ", " << zeroVoxel[2] << ")\n";
    std::cout << "Expected: (32, 132, 39) = (X, Y, Z)\n";
    
    // Cross-test: hiRes voxel -> world -> loRes voxel (MINC order)
    std::cout << "\n=== Cross test: hiRes voxel -> world -> loRes voxel ===\n";
    int hiResVoxel[3] = {96, 114, 78};  // MINC order: X, Y, Z
    double crossWorld[3];
    hiRes.transformVoxelToWorld(hiResVoxel, crossWorld);
    std::cout << "hiRes: voxel (" << hiResVoxel[0] << ", " << hiResVoxel[1] << ", " << hiResVoxel[2] << ") -> world ("
              << crossWorld[0] << ", " << crossWorld[1] << ", " << crossWorld[2] << ")\n";
    
    int loResVoxel[3];
    loRes.transformWorldToVoxel(crossWorld, loResVoxel);
    std::cout << "loRes: world -> voxel (" << loResVoxel[0] << ", " << loResVoxel[1] << ", " << loResVoxel[2] << ")\n";
    
    // Verify: loRes world from loRes voxel should match
    double loResWorld[3];
    loRes.transformVoxelToWorld(loResVoxel, loResWorld);
    std::cout << "loRes: voxel -> world (" << loResWorld[0] << ", " << loResWorld[1] << ", " << loResWorld[2] << ")\n";
    std::cout << "Cross-check: worlds match = " << (std::abs(crossWorld[0]-loResWorld[0])<0.1 && std::abs(crossWorld[1]-loResWorld[1])<0.1 && std::abs(crossWorld[2]-loResWorld[2])<0.1 ? "YES" : "NO") << "\n";
    
    return 0;
}
