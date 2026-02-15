#include "Volume.h"
#include <iostream>
#include <cmath>
#include <filesystem>

int main() {
    std::filesystem::path testDataDir;
    if (std::filesystem::exists("/app/test_data")) {
        testDataDir = "/app/test_data";
    } else if (std::filesystem::exists(std::filesystem::current_path() / "test_data")) {
        testDataDir = std::filesystem::current_path() / "test_data";
    } else {
        std::cerr << "Test data directory not found\n";
        return 1;
    }
    
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
    std::cout << "Dimensions: " << hiRes.dimensions[0] << "x" << hiRes.dimensions[1] << "x" << hiRes.dimensions[2] << "\n";
    std::cout << "Step: " << hiRes.step[0] << ", " << hiRes.step[1] << ", " << hiRes.step[2] << "\n";
    std::cout << "Start: " << hiRes.start[0] << ", " << hiRes.start[1] << ", " << hiRes.start[2] << "\n";
    
    std::cout << "\n=== Volume 2 (thick-slices) ===\n";
    std::cout << "Dimensions: " << loRes.dimensions[0] << "x" << loRes.dimensions[1] << "x" << loRes.dimensions[2] << "\n";
    std::cout << "Step: " << loRes.step[0] << ", " << loRes.step[1] << ", " << loRes.step[2] << "\n";
    std::cout << "Start: " << loRes.start[0] << ", " << loRes.start[1] << ", " << loRes.start[2] << "\n";
    
    // Test world coordinate (0, 0, 0)
    double worldPos[3] = {0.0, 0.0, 0.0};
    
    std::cout << "\n=== Test: World (0,0,0) -> Voxel (MINC X,Y,Z order) ===\n";
    
    // For hiRes: expected voxel (96, 132, 78) in MINC X,Y,Z order
    int voxelHiRes[3];
    hiRes.transformWorldToVoxel(worldPos, voxelHiRes);
    std::cout << "Volume 1 (high-res):\n";
    std::cout << "  World: (" << worldPos[0] << ", " << worldPos[1] << ", " << worldPos[2] << ")\n";
    std::cout << "  Voxel (X,Y,Z): (" << voxelHiRes[0] << ", " << voxelHiRes[1] << ", " << voxelHiRes[2] << ")\n";
    std::cout << "  Expected: (96, 132, 78)\n";
    
    // For loRes: expected voxel (32, 132, 39) in MINC X,Y,Z order
    // Math: voxelX = (0-(-95))/3 = 31.67 -> 32, voxelY = 132, voxelZ = (0-(-77.5))/2 = 38.75 -> 39
    int voxelLoResExact[3];
    loRes.transformWorldToVoxel(worldPos, voxelLoResExact);
    std::cout << "Volume 2 (thick-slices):\n";
    std::cout << "  World: (" << worldPos[0] << ", " << worldPos[1] << ", " << worldPos[2] << ")\n";
    std::cout << "  Voxel (X,Y,Z rounded): (" << voxelLoResExact[0] << ", " << voxelLoResExact[1] << ", " << voxelLoResExact[2] << ")\n";
    std::cout << "  Expected: (31.666..., 132, 38.75) -> rounded to (32, 132, 39)\n";
    
    // Now test the sync logic in reverse: voxel -> world -> voxel (MINC order)
    std::cout << "\n=== Test: Voxel -> World -> Voxel round trip ===\n";
    
    // For hiRes: start with MINC voxel (96, 132, 78) = X,Y,Z
    int hiResVoxel[3] = {96, 132, 78};
    double hiResWorld[3];
    hiRes.transformVoxelToWorld(hiResVoxel, hiResWorld);
    std::cout << "Volume 1: voxel (" << hiResVoxel[0] << ", " << hiResVoxel[1] << ", " << hiResVoxel[2] << ") -> world ("
              << hiResWorld[0] << ", " << hiResWorld[1] << ", " << hiResWorld[2] << ")\n";
    
    // Convert that world to loRes voxel
    int loResVoxel[3];
    loRes.transformWorldToVoxel(hiResWorld, loResVoxel);
    std::cout << "Volume 2: world (" << hiResWorld[0] << ", " << hiResWorld[1] << ", " << hiResWorld[2] << ") -> voxel ("
              << loResVoxel[0] << ", " << loResVoxel[1] << ", " << loResVoxel[2] << ")\n";
    std::cout << "Expected: (32, 132, 39) or nearby (MINC X,Y,Z)\n";
    
    // Verify round-trip: loRes voxel -> world -> hiRes voxel
    double loResWorld[3];
    loRes.transformVoxelToWorld(loResVoxel, loResWorld);
    std::cout << "Round-trip check: Volume 2 voxel (" << loResVoxel[0] << ", " << loResVoxel[1] << ", " << loResVoxel[2] 
              << ") -> world (" << loResWorld[0] << ", " << loResWorld[1] << ", " << loResWorld[2] << ")\n";
    
    // === Assertions ===
    int errors = 0;
    
    // Volume 1: world(0,0,0) -> voxel(96, 132, 78) in MINC X,Y,Z
    if (voxelHiRes[0] != 96 || voxelHiRes[1] != 132 || voxelHiRes[2] != 78)
    {
        std::cerr << "FAIL: HiRes world(0,0,0) -> voxel expected (96,132,78), got ("
                  << voxelHiRes[0] << "," << voxelHiRes[1] << "," << voxelHiRes[2] << ")\n";
        errors++;
    }
    
    // Volume 2: world(0,0,0) -> voxel(32, 132, 39) in MINC X,Y,Z
    if (voxelLoResExact[0] != 32 || voxelLoResExact[1] != 132 || voxelLoResExact[2] != 39)
    {
        std::cerr << "FAIL: LoRes world(0,0,0) -> voxel expected (32,132,39), got ("
                  << voxelLoResExact[0] << "," << voxelLoResExact[1] << "," << voxelLoResExact[2] << ")\n";
        errors++;
    }
    
    // Volume 1 center: voxel(96,132,78) -> world(0,0,0)
    if (std::abs(hiResWorld[0]) > 0.01 || std::abs(hiResWorld[1]) > 0.01 || std::abs(hiResWorld[2]) > 0.01)
    {
        std::cerr << "FAIL: HiRes voxel(96,132,78) -> world expected (0,0,0), got ("
                  << hiResWorld[0] << "," << hiResWorld[1] << "," << hiResWorld[2] << ")\n";
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
