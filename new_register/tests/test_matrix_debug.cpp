#include "Volume.h"
#include <iostream>
#include <cmath>
#include <filesystem>
#include <glm/glm.hpp>

int main() {
    std::filesystem::path testDataDir = "/app/test_data";

    Volume hiRes, loRes;
    hiRes.load((testDataDir / "mni_icbm152_t1_tal_nlin_sym_09c.mnc").string());
    loRes.load((testDataDir / "mni_icbm152_t1_tal_nlin_sym_09c_thick_slices.mnc").string());
    
    int failures = 0;
    
    std::cout << "=== Volume 1 (high-res) ===\n";
    std::cout << "dimensions (X,Y,Z): " << hiRes.dimensions.x << ", " << hiRes.dimensions.y << ", " << hiRes.dimensions.z << "\n";
    std::cout << "step (X,Y,Z): " << hiRes.step.x << ", " << hiRes.step.y << ", " << hiRes.step.z << "\n";
    std::cout << "start (X,Y,Z): " << hiRes.start.x << ", " << hiRes.start.y << ", " << hiRes.start.z << "\n";
    
    // Verify hiRes dimensions
    if (hiRes.dimensions.x != 193 || hiRes.dimensions.y != 229 || hiRes.dimensions.z != 193) {
        std::cerr << "FAIL: hiRes dimensions expected 193x229x193\n";
        failures++;
    }
    // Verify hiRes step
    if (std::abs(hiRes.step.x - 1.0) > 1e-6 || std::abs(hiRes.step.y - 1.0) > 1e-6 || std::abs(hiRes.step.z - 1.0) > 1e-6) {
        std::cerr << "FAIL: hiRes step expected 1x1x1\n";
        failures++;
    }
    // Verify hiRes start
    if (std::abs(hiRes.start.x - (-96.0)) > 1e-6 || 
        std::abs(hiRes.start.y - (-132.0)) > 1e-6 || 
        std::abs(hiRes.start.z - (-78.0)) > 1e-6) {
        std::cerr << "FAIL: hiRes start expected -96x-132x-78\n";
        failures++;
    }
    
    std::cout << "\n=== Volume 2 (thick-slices) ===\n";
    std::cout << "dimensions (X,Y,Z): " << loRes.dimensions.x << ", " << loRes.dimensions.y << ", " << loRes.dimensions.z << "\n";
    std::cout << "step (X,Y,Z): " << loRes.step.x << ", " << loRes.step.y << ", " << loRes.step.z << "\n";
    std::cout << "start (X,Y,Z): " << loRes.start.x << ", " << loRes.start.y << ", " << loRes.start.z << "\n";
    
    // Verify loRes dimensions
    if (loRes.dimensions.x != 64 || loRes.dimensions.y != 229 || loRes.dimensions.z != 96) {
        std::cerr << "FAIL: loRes dimensions expected 64x229x96\n";
        failures++;
    }
    // Verify loRes step
    if (std::abs(loRes.step.x - 3.0) > 1e-6 || 
        std::abs(loRes.step.y - 1.0) > 1e-6 || 
        std::abs(loRes.step.z - 2.0) > 1e-6) {
        std::cerr << "FAIL: loRes step expected 3x1x2\n";
        failures++;
    }
    // Verify loRes start
    if (std::abs(loRes.start.x - (-95.0)) > 1e-6 || 
        std::abs(loRes.start.y - (-132.0)) > 1e-6 || 
        std::abs(loRes.start.z - (-77.5)) > 1e-6) {
        std::cerr << "FAIL: loRes start expected -95x-132x-77.5\n";
        failures++;
    }
    
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
    
    // Verify voxel->world transformation
    if (std::abs(world.x - expectedWorld.x) > 1e-6 ||
        std::abs(world.y - expectedWorld.y) > 1e-6 ||
        std::abs(world.z - expectedWorld.z) > 1e-6) {
        std::cerr << "FAIL: hiRes voxel->world transformation failed\n";
        failures++;
    }
    
    // Convert back
    glm::ivec3 backVoxel;
    hiRes.transformWorldToVoxel(world, backVoxel);
    std::cout << "Round-trip: world -> voxel (" << backVoxel.x << ", " << backVoxel.y << ", " << backVoxel.z << ")\n";
    
    // Verify round-trip
    if (backVoxel.x != testVoxel.x || backVoxel.y != testVoxel.y || backVoxel.z != testVoxel.z) {
        std::cerr << "FAIL: hiRes round-trip failed (expected " 
                  << testVoxel.x << "," << testVoxel.y << "," << testVoxel.z << " got "
                  << backVoxel.x << "," << backVoxel.y << "," << backVoxel.z << ")\n";
        failures++;
    }
    
    // Test with world (0,0,0)
    std::cout << "\n=== Test: world (0,0,0) -> voxel ===\n";
    glm::dvec3 zeroWorld(0, 0, 0);
    glm::ivec3 zeroVoxel;
    hiRes.transformWorldToVoxel(zeroWorld, zeroVoxel);
    std::cout << "hiRes: world (0,0,0) -> voxel (" << zeroVoxel.x << ", " << zeroVoxel.y << ", " << zeroVoxel.z << ")\n";
    std::cout << "Expected: (96, 132, 78) = (X, Y, Z)\n";
    
    // Verify world (0,0,0) -> voxel (96, 132, 78)
    if (zeroVoxel.x != 96 || zeroVoxel.y != 132 || zeroVoxel.z != 78) {
        std::cerr << "FAIL: hiRes world(0,0,0)->voxel expected (96,132,78)\n";
        failures++;
    }
    
    loRes.transformWorldToVoxel(zeroWorld, zeroVoxel);
    std::cout << "loRes: world (0,0,0) -> voxel (" << zeroVoxel.x << ", " << zeroVoxel.y << ", " << zeroVoxel.z << ")\n";
    std::cout << "Expected: (32, 132, 39) = (X, Y, Z)\n";
    
    // Verify world (0,0,0) -> voxel (32, 132, 39)
    if (zeroVoxel.x != 32 || zeroVoxel.y != 132 || zeroVoxel.z != 39) {
        std::cerr << "FAIL: loRes world(0,0,0)->voxel expected (32,132,39)\n";
        failures++;
    }
    
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
    
    // Allow 2mm tolerance due to different resolutions (loRes has step 3,1,2)
    // This is expected behavior - different resolutions can't represent the same world exactly
    bool worldsMatch = std::abs(crossWorld.x-loResWorld.x) < 2.0 && 
                       std::abs(crossWorld.y-loResWorld.y) < 2.0 && 
                       std::abs(crossWorld.z-loResWorld.z) < 2.0;
    std::cout << "Cross-check: worlds match = " << (worldsMatch ? "YES" : "NO") << "\n";
    
    if (!worldsMatch) {
        std::cerr << "FAIL: Cross-volume sync test failed (world coordinates don't match within 2mm tolerance)\n";
        failures++;
    }
    
    if (failures > 0) {
        std::cerr << failures << " test(s) failed!\n";
        return 1;
    }
    
    std::cout << "\nAll tests passed!\n";
    return 0;
}
