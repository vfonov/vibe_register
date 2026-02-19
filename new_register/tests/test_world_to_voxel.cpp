#include "Volume.h"
#include <iostream>
#include <cmath>
#include <filesystem>
#include <glm/glm.hpp>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <test_data_dir>\n";
        return 1;
    }
    std::filesystem::path testDataDir = argv[1];
    
    std::string hiResPath = (testDataDir / "mni_icbm152_t1_tal_nlin_sym_09c.mnc").string();
    std::string loResPath = (testDataDir / "mni_icbm152_t1_tal_nlin_sym_09c_thick_slices.mnc").string();

    Volume hiRes;
    try {
        hiRes.load(hiResPath);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load high-res volume: " << e.what() << "\n";
        return 1;
    }

    Volume loRes;
    try {
        loRes.load(loResPath);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load low-res volume: " << e.what() << "\n";
        return 1;
    }
    
    std::cout << "=== Volume 1 (high-res) ===\n";
    std::cout << "Dimensions: " << hiRes.dimensions.x << "x" << hiRes.dimensions.y << "x" << hiRes.dimensions.z << "\n";
    std::cout << "Step: " << hiRes.step.x << ", " << hiRes.step.y << ", " << hiRes.step.z << "\n";
    std::cout << "Start: " << hiRes.start.x << ", " << hiRes.start.y << ", " << hiRes.start.z << "\n";
    
    std::cout << "\n=== Volume 2 (thick-slices) ===\n";
    std::cout << "Dimensions: " << loRes.dimensions.x << "x" << loRes.dimensions.y << "x" << loRes.dimensions.z << "\n";
    std::cout << "Step: " << loRes.step.x << ", " << loRes.step.y << ", " << loRes.step.z << "\n";
    std::cout << "Start: " << loRes.start.x << ", " << loRes.start.y << ", " << loRes.start.z << "\n";
    
    // Test world coordinate (0, 0, 0)
    glm::dvec3 worldPos(0.0, 0.0, 0.0);
    
    std::cout << "\n=== Test: World (0,0,0) -> Voxel (MINC X,Y,Z order) ===\n";
    
    // For hiRes: expected voxel (96, 132, 78) in MINC X,Y,Z order
    glm::ivec3 voxelHiRes;
    hiRes.transformWorldToVoxel(worldPos, voxelHiRes);
    std::cout << "Volume 1 (high-res):\n";
    std::cout << "  World: (" << worldPos.x << ", " << worldPos.y << ", " << worldPos.z << ")\n";
    std::cout << "  Voxel (X,Y,Z): (" << voxelHiRes.x << ", " << voxelHiRes.y << ", " << voxelHiRes.z << ")\n";
    std::cout << "  Expected: (96, 132, 78)\n";
    
    // For loRes: expected voxel (32, 132, 39) in MINC X,Y,Z order
    // Math: voxelX = (0-(-95))/3 = 31.67 -> 32, voxelY = 132, voxelZ = (0-(-77.5))/2 = 38.75 -> 39
    glm::ivec3 voxelLoResExact;
    loRes.transformWorldToVoxel(worldPos, voxelLoResExact);
    std::cout << "Volume 2 (thick-slices):\n";
    std::cout << "  World: (" << worldPos.x << ", " << worldPos.y << ", " << worldPos.z << ")\n";
    std::cout << "  Voxel (X,Y,Z rounded): (" << voxelLoResExact.x << ", " << voxelLoResExact.y << ", " << voxelLoResExact.z << ")\n";
    std::cout << "  Expected: (31.666..., 132, 38.75) -> rounded to (32, 132, 39)\n";
    
    // Now test the sync logic in reverse: voxel -> world -> voxel (MINC order)
    std::cout << "\n=== Test: Voxel -> World -> Voxel round trip ===\n";
    
    // For hiRes: start with MINC voxel (96, 132, 78) = X,Y,Z
    glm::ivec3 hiResVoxel(96, 132, 78);
    glm::dvec3 hiResWorld;
    hiRes.transformVoxelToWorld(hiResVoxel, hiResWorld);
    std::cout << "Volume 1: voxel (" << hiResVoxel.x << ", " << hiResVoxel.y << ", " << hiResVoxel.z << ") -> world ("
              << hiResWorld.x << ", " << hiResWorld.y << ", " << hiResWorld.z << ")\n";
    
    // Convert that world to loRes voxel
    glm::ivec3 loResVoxel;
    loRes.transformWorldToVoxel(hiResWorld, loResVoxel);
    std::cout << "Volume 2: world (" << hiResWorld.x << ", " << hiResWorld.y << ", " << hiResWorld.z << ") -> voxel ("
              << loResVoxel.x << ", " << loResVoxel.y << ", " << loResVoxel.z << ")\n";
    std::cout << "Expected: (32, 132, 39) or nearby (MINC X,Y,Z)\n";
    
    // Verify round-trip: loRes voxel -> world -> hiRes voxel
    glm::dvec3 loResWorld;
    loRes.transformVoxelToWorld(loResVoxel, loResWorld);
    std::cout << "Round-trip check: Volume 2 voxel (" << loResVoxel.x << ", " << loResVoxel.y << ", " << loResVoxel.z 
              << ") -> world (" << loResWorld.x << ", " << loResWorld.y << ", " << loResWorld.z << ")\n";
    
    // === Assertions ===
    int errors = 0;
    
    // Volume 1: world(0,0,0) -> voxel(96, 132, 78) in MINC X,Y,Z
    if (voxelHiRes.x != 96 || voxelHiRes.y != 132 || voxelHiRes.z != 78)
    {
        std::cerr << "FAIL: HiRes world(0,0,0) -> voxel expected (96,132,78), got ("
                  << voxelHiRes.x << "," << voxelHiRes.y << "," << voxelHiRes.z << ")\n";
        errors++;
    }
    
    // Volume 2: world(0,0,0) -> voxel(32, 132, 39) in MINC X,Y,Z
    if (voxelLoResExact.x != 32 || voxelLoResExact.y != 132 || voxelLoResExact.z != 39)
    {
        std::cerr << "FAIL: LoRes world(0,0,0) -> voxel expected (32,132,39), got ("
                  << voxelLoResExact.x << "," << voxelLoResExact.y << "," << voxelLoResExact.z << ")\n";
        errors++;
    }
    
    // Volume 1 center: voxel(96,132,78) -> world(0,0,0)
    if (std::abs(hiResWorld.x) > 0.01 || std::abs(hiResWorld.y) > 0.01 || std::abs(hiResWorld.z) > 0.01)
    {
        std::cerr << "FAIL: HiRes voxel(96,132,78) -> world expected (0,0,0), got ("
                  << hiResWorld.x << "," << hiResWorld.y << "," << hiResWorld.z << ")\n";
        errors++;
    }
    
    if (errors > 0)
    {
        std::cerr << errors << " test(s) failed!\n";
        return 1;
    }
    
    std::cout << "\nAll assertions passed.\n";
    return 0;
}
