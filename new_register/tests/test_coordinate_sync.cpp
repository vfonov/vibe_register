#include "Volume.h"
#include <iostream>
#include <cmath>
#include <filesystem>
#include <glm/glm.hpp>

bool near(double a, double b, double tol = 1e-6) {
    return std::abs(a - b) < tol;
}

// Simplified sync: use all 3 voxel coordinates from reference, convert to world, then to other volume
// All vectors use MINC order: .x = X, .y = Y, .z = Z
void syncCursors(const Volume& refVol, const glm::ivec3& refVoxelMINC,
                 const Volume& otherVol, glm::ivec3& otherVoxelMINC) {
    
    // Get world position from reference volume
    glm::dvec3 worldPos;
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
    
    std::cout << "High-res volume: " << hiRes.dimensions.x << "x" << hiRes.dimensions.y << "x" << hiRes.dimensions.z << "\n";
    std::cout << "  step: " << hiRes.step.x << ", " << hiRes.step.y << ", " << hiRes.step.z << "\n";
    std::cout << "  start: " << hiRes.start.x << ", " << hiRes.start.y << ", " << hiRes.start.z << "\n";
    std::cout << "Low-res volume: " << loRes.dimensions.x << "x" << loRes.dimensions.y << "x" << loRes.dimensions.z << "\n";
    std::cout << "  step: " << loRes.step.x << ", " << loRes.step.y << ", " << loRes.step.z << "\n";
    std::cout << "  start: " << loRes.start.x << ", " << loRes.start.y << ", " << loRes.start.z << "\n";
    
    int testPass = 0;
    int testFail = 0;
    
    // Test: Take voxel coordinates from hiRes, convert to world, then to loRes
    // Then verify that converting back gives us approximately the same world coordinates
    std::cout << "\n=== Test: Full voxel coordinate sync ===\n";
    {
        // Use some arbitrary voxel coordinates in MINC order .x = X, .y = Y, .z = Z
        glm::ivec3 refSlice(100, 80, 50);  // X=100, Y=80, Z=50
        
        glm::dvec3 worldPos;
        hiRes.transformVoxelToWorld(refSlice, worldPos);
        std::cout << "Ref (hiRes) voxel MINC (X,Y,Z): (" << refSlice.x << ", " << refSlice.y << ", " << refSlice.z << ")\n";
        std::cout << "World position: (" << worldPos.x << ", " << worldPos.y << ", " << worldPos.z << ")\n";
        
        glm::ivec3 otherSlice;
        loRes.transformWorldToVoxel(worldPos, otherSlice);
        std::cout << "Other (loRes) voxel MINC (X,Y,Z): (" << otherSlice.x << ", " << otherSlice.y << ", " << otherSlice.z << ")\n";
        
        // Verify: convert back to world and check if it matches
        glm::dvec3 worldPos2;
        loRes.transformVoxelToWorld(otherSlice, worldPos2);
        std::cout << "World from other: (" << worldPos2.x << ", " << worldPos2.y << ", " << worldPos2.z << ")\n";
        
        // The world coordinates should be very close (within voxel size tolerance)
        double tol = 2.0; // Allow 2mm tolerance due to different resolutions
        if (near(worldPos.x, worldPos2.x, tol) && 
            near(worldPos.y, worldPos2.y, tol) && 
            near(worldPos.z, worldPos2.z, tol)) {
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
        // MINC order: .x = X, .y = Y, .z = Z
        glm::ivec3 refSlice(hiRes.dimensions.x/2, hiRes.dimensions.y/2, hiRes.dimensions.z/2);
        
        glm::dvec3 worldPos;
        hiRes.transformVoxelToWorld(refSlice, worldPos);
        std::cout << "Ref (hiRes) voxel MINC (X,Y,Z): (" << refSlice.x << ", " << refSlice.y << ", " << refSlice.z << ")\n";
        std::cout << "World position: (" << worldPos.x << ", " << worldPos.y << ", " << worldPos.z << ")\n";
        
        glm::ivec3 otherSlice;
        loRes.transformWorldToVoxel(worldPos, otherSlice);
        std::cout << "Other (loRes) voxel MINC (X,Y,Z): (" << otherSlice.x << ", " << otherSlice.y << ", " << otherSlice.z << ")\n";
        
        glm::dvec3 worldPos2;
        loRes.transformVoxelToWorld(otherSlice, worldPos2);
        std::cout << "World from other: (" << worldPos2.x << ", " << worldPos2.y << ", " << worldPos2.z << ")\n";
        
        double tol = 2.0;
        if (near(worldPos.x, worldPos2.x, tol) && 
            near(worldPos.y, worldPos2.y, tol) && 
            near(worldPos.z, worldPos2.z, tol)) {
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
