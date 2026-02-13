#include "Volume.h"
#include <iostream>
#include <cmath>
#include <filesystem>

// Helper to compare doubles with tolerance
bool near(double a, double b, double tol = 1e-6) {
    return std::abs(a - b) < tol;
}

int main() {
    // Get test data paths
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

    // Load high-res volume (1mm isotropic)
    Volume hiRes;
    try {
        hiRes.load(hiResPath);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load high-res volume: " << e.what() << "\n";
        return 1;
    }

    // Load low-res volume (3x1x2 mm voxels)
    Volume loRes;
    try {
        loRes.load(loResPath);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load low-res volume: " << e.what() << "\n";
        return 1;
    }

    std::cout << "Test Case 3: Verify matrix transformation matches direct formula\n";
    
    int testVoxel[] = {10, 20, 30};
    double direct[3];
    direct[0] = hiRes.start[0] + (testVoxel[0] + 0.5) * hiRes.step[0];
    direct[1] = hiRes.start[1] + (testVoxel[1] + 0.5) * hiRes.step[1];
    direct[2] = hiRes.start[2] + (testVoxel[2] + 0.5) * hiRes.step[2];
    
    double matrix[3];
    hiRes.transformVoxelToWorld(testVoxel, matrix);
    
    std::cout << "  Test voxel " << testVoxel[0] << "," << testVoxel[1] << "," << testVoxel[2] << "\n";
    std::cout << "  Direct calculation: " << direct[0] << ", " << direct[1] << ", " << direct[2] << "\n";
    std::cout << "  Matrix transform: " << matrix[0] << ", " << matrix[1] << ", " << matrix[2] << "\n";
    std::cout << "  Match: " << (near(direct[0], matrix[0]) && near(direct[1], matrix[1]) && near(direct[2], matrix[2]) ? "YES" : "NO") << "\n";
    
    return (near(direct[0], matrix[0]) && near(direct[1], matrix[1]) && near(direct[2], matrix[2])) ? 0 : 1;
}
