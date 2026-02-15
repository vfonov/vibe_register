#include "Volume.h"
#include <iostream>
#include <cmath>
#include <filesystem>

bool near(double a, double b, double tol = 1e-6) {
    return std::abs(a - b) < tol;
}

// Simplified sync: use all 3 voxel coordinates from reference, convert to world, then to other volume
// All arrays use MINC order: [0]=X, [1]=Y, [2]=Z
void syncCursors(const Volume& refVol, int refVoxelMINC[3],
                 const Volume& otherVol, int otherVoxelMINC[3]) {
    
    // Get world position from reference volume
    double worldPos[3];
    refVol.transformVoxelToWorld(refVoxelMINC, worldPos);
    
    // Convert world -> voxel for other volume
    otherVol.transformWorldToVoxel(worldPos, otherVoxelMINC);
}

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
    
    std::cout << "High-res volume: " << hiRes.dimensions[0] << "x" << hiRes.dimensions[1] << "x" << hiRes.dimensions[2] << "\n";
    std::cout << "  step: " << hiRes.step[0] << ", " << hiRes.step[1] << ", " << hiRes.step[2] << "\n";
    std::cout << "  start: " << hiRes.start[0] << ", " << hiRes.start[1] << ", " << hiRes.start[2] << "\n";
    std::cout << "Low-res volume: " << loRes.dimensions[0] << "x" << loRes.dimensions[1] << "x" << loRes.dimensions[2] << "\n";
    std::cout << "  step: " << loRes.step[0] << ", " << loRes.step[1] << ", " << loRes.step[2] << "\n";
    std::cout << "  start: " << loRes.start[0] << ", " << loRes.start[1] << ", " << loRes.start[2] << "\n";
    
    int testPass = 0;
    int testFail = 0;
    
    // Test: Take voxel coordinates from hiRes, convert to world, then to loRes
    // Then verify that converting back gives us approximately the same world coordinates
    std::cout << "\n=== Test: Full voxel coordinate sync ===\n";
    {
        // Use some arbitrary voxel coordinates in MINC order [0]=X, [1]=Y, [2]=Z
        int refSlice[3] = {100, 80, 50};  // X=100, Y=80, Z=50
        
        double worldPos[3];
        hiRes.transformVoxelToWorld(refSlice, worldPos);
        std::cout << "Ref (hiRes) voxel MINC (X,Y,Z): (" << refSlice[0] << ", " << refSlice[1] << ", " << refSlice[2] << ")\n";
        std::cout << "World position: (" << worldPos[0] << ", " << worldPos[1] << ", " << worldPos[2] << ")\n";
        
        int otherSlice[3];
        loRes.transformWorldToVoxel(worldPos, otherSlice);
        std::cout << "Other (loRes) voxel MINC (X,Y,Z): (" << otherSlice[0] << ", " << otherSlice[1] << ", " << otherSlice[2] << ")\n";
        
        // Verify: convert back to world and check if it matches
        double worldPos2[3];
        loRes.transformVoxelToWorld(otherSlice, worldPos2);
        std::cout << "World from other: (" << worldPos2[0] << ", " << worldPos2[1] << ", " << worldPos2[2] << ")\n";
        
        // The world coordinates should be very close (within voxel size tolerance)
        double tol = 2.0; // Allow 2mm tolerance due to different resolutions
        if (near(worldPos[0], worldPos2[0], tol) && 
            near(worldPos[1], worldPos2[1], tol) && 
            near(worldPos[2], worldPos2[2], tol)) {
            std::cout << "PASS: World coordinates match\n";
            testPass++;
        } else {
            std::cout << "FAIL: World coordinates don't match\n";
            testFail++;
        }
    }
    
    // Test 2: Center voxel
    std::cout << "\n=== Test: Center voxel sync ===\n";
    {
        // MINC order: [0]=X, [1]=Y, [2]=Z
        int refSlice[3] = {hiRes.dimensions[0]/2, hiRes.dimensions[1]/2, hiRes.dimensions[2]/2};
        
        double worldPos[3];
        hiRes.transformVoxelToWorld(refSlice, worldPos);
        std::cout << "Ref (hiRes) voxel MINC (X,Y,Z): (" << refSlice[0] << ", " << refSlice[1] << ", " << refSlice[2] << ")\n";
        std::cout << "World position: (" << worldPos[0] << ", " << worldPos[1] << ", " << worldPos[2] << ")\n";
        
        int otherSlice[3];
        loRes.transformWorldToVoxel(worldPos, otherSlice);
        std::cout << "Other (loRes) voxel MINC (X,Y,Z): (" << otherSlice[0] << ", " << otherSlice[1] << ", " << otherSlice[2] << ")\n";
        
        double worldPos2[3];
        loRes.transformVoxelToWorld(otherSlice, worldPos2);
        std::cout << "World from other: (" << worldPos2[0] << ", " << worldPos2[1] << ", " << worldPos2[2] << ")\n";
        
        double tol = 2.0;
        if (near(worldPos[0], worldPos2[0], tol) && 
            near(worldPos[1], worldPos2[1], tol) && 
            near(worldPos[2], worldPos2[2], tol)) {
            std::cout << "PASS: World coordinates match\n";
            testPass++;
        } else {
            std::cout << "FAIL: World coordinates don't match\n";
            testFail++;
        }
    }
    
    std::cout << "\n=== Summary: " << testPass << " passed, " << testFail << " failed ===\n";
    
    return testFail > 0 ? 1 : 0;
}
