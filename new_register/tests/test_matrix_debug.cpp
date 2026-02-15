#include "Volume.h"
#include <iostream>
#include <cmath>
#include <filesystem>
#include <glm/glm.hpp>

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
    std::cout << "dimensions (X,Y,Z): " << hiRes.dimensions.x << ", " << hiRes.dimensions.y << ", " << hiRes.dimensions.z << "\n";
    std::cout << "step (X,Y,Z): " << hiRes.step.x << ", " << hiRes.step.y << ", " << hiRes.step.z << "\n";
    std::cout << "start (X,Y,Z): " << hiRes.start.x << ", " << hiRes.start.y << ", " << hiRes.start.z << "\n";
    printMatrix(hiRes.voxelToWorld, "voxelToWorld");
    printMatrix(hiRes.worldToVoxel, "worldToVoxel");
    
    std::cout << "\n=== Volume 2 (thick-slices) ===\n";
    std::cout << "dimensions (X,Y,Z): " << loRes.dimensions.x << ", " << loRes.dimensions.y << ", " << loRes.dimensions.z << "\n";
    std::cout << "step (X,Y,Z): " << loRes.step.x << ", " << loRes.step.y << ", " << loRes.step.z << "\n";
    std::cout << "start (X,Y,Z): " << loRes.start.x << ", " << loRes.start.y << ", " << loRes.start.z << "\n";
    printMatrix(loRes.voxelToWorld, "voxelToWorld");
    printMatrix(loRes.worldToVoxel, "worldToVoxel");
    
    // Test: Verify round-trip
    std::cout << "\n=== Test round-trip: voxel -> world -> voxel ===\n";
    
    // For hiRes: test voxel (96, 114, 78) which should be center
    glm::ivec3 testVoxel(96, 114, 78);  // X=96, Y=114, Z=78 in MINC convention
    glm::dvec3 world;
    
    // Manual calculation: world = start + voxel * step
    glm::dvec3 expectedWorld;
    expectedWorld.x = hiRes.start.x + testVoxel.x * hiRes.step.x;
    expectedWorld.y = hiRes.start.y + testVoxel.y * hiRes.step.y;
    expectedWorld.z = hiRes.start.z + testVoxel.z * hiRes.step.z;
    std::cout << "Manual: voxel (" << testVoxel.x << ", " << testVoxel.y << ", " << testVoxel.z << ") -> world ("
              << expectedWorld.x << ", " << expectedWorld.y << ", " << expectedWorld.z << ")\n";
    
    // Using the function (MINC order: X, Y, Z)
    glm::ivec3 mincVoxel(96, 114, 78);  // X=96, Y=114, Z=78 in MINC convention
    hiRes.transformVoxelToWorld(mincVoxel, world);
    std::cout << "transformVoxelToWorld: voxel (" << mincVoxel.x << ", " << mincVoxel.y << ", " << mincVoxel.z << ") -> world ("
              << world.x << ", " << world.y << ", " << world.z << ")\n";
    
    // Convert back
    glm::ivec3 backVoxel;
    hiRes.transformWorldToVoxel(world, backVoxel);
    std::cout << "Round-trip: world -> voxel (" << backVoxel.x << ", " << backVoxel.y << ", " << backVoxel.z << ")\n";
    
    // Test with world (0,0,0)
    std::cout << "\n=== Test: world (0,0,0) -> voxel ===\n";
    glm::dvec3 zeroWorld(0, 0, 0);
    glm::ivec3 zeroVoxel;
    hiRes.transformWorldToVoxel(zeroWorld, zeroVoxel);
    std::cout << "hiRes: world (0,0,0) -> voxel (" << zeroVoxel.x << ", " << zeroVoxel.y << ", " << zeroVoxel.z << ")\n";
    std::cout << "Expected: (96, 132, 78) = (X, Y, Z)\n";
    
    loRes.transformWorldToVoxel(zeroWorld, zeroVoxel);
    std::cout << "loRes: world (0,0,0) -> voxel (" << zeroVoxel.x << ", " << zeroVoxel.y << ", " << zeroVoxel.z << ")\n";
    std::cout << "Expected: (32, 132, 39) = (X, Y, Z)\n";
    
    // Cross-test: hiRes voxel -> world -> loRes voxel (MINC order)
    std::cout << "\n=== Cross test: hiRes voxel -> world -> loRes voxel ===\n";
    glm::ivec3 hiResVoxel(96, 114, 78);  // MINC order: X, Y, Z
    glm::dvec3 crossWorld;
    hiRes.transformVoxelToWorld(hiResVoxel, crossWorld);
    std::cout << "hiRes: voxel (" << hiResVoxel.x << ", " << hiResVoxel.y << ", " << hiResVoxel.z << ") -> world ("
              << crossWorld.x << ", " << crossWorld.y << ", " << crossWorld.z << ")\n";
    
    glm::ivec3 loResVoxel;
    loRes.transformWorldToVoxel(crossWorld, loResVoxel);
    std::cout << "loRes: world -> voxel (" << loResVoxel.x << ", " << loResVoxel.y << ", " << loResVoxel.z << ")\n";
    
    // Verify: loRes world from loRes voxel should match
    glm::dvec3 loResWorld;
    loRes.transformVoxelToWorld(loResVoxel, loResWorld);
    std::cout << "loRes: voxel -> world (" << loResWorld.x << ", " << loResWorld.y << ", " << loResWorld.z << ")\n";
    std::cout << "Cross-check: worlds match = " << (std::abs(crossWorld.x-loResWorld.x)<0.1 && std::abs(crossWorld.y-loResWorld.y)<0.1 && std::abs(crossWorld.z-loResWorld.z)<0.1 ? "YES" : "NO") << "\n";
    
    return 0;
}
