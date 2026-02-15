#include <iostream>
#include <minc2-simple.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <cmath>

int main() {
    const char* file1 = "/app/test_data/mni_icbm152_t1_tal_nlin_sym_09c.mnc";
    const char* file2 = "/app/test_data/mni_icbm152_t1_tal_nlin_sym_09c_thick_slices.mnc";
    
    minc2_file_handle h1 = nullptr, h2 = nullptr;
    
    if (minc2_allocate(&h1) != MINC2_SUCCESS) {
        std::cerr << "Failed to allocate handle 1\n";
        return 1;
    }
    if (minc2_allocate(&h2) != MINC2_SUCCESS) {
        std::cerr << "Failed to allocate handle 2\n";
        return 1;
    }
    
    if (minc2_open(h1, file1) != MINC2_SUCCESS) {
        std::cerr << "Failed to open " << file1 << "\n";
        return 1;
    }
    if (minc2_open(h2, file2) != MINC2_SUCCESS) {
        std::cerr << "Failed to open " << file2 << "\n";
        return 1;
    }
    
    if (minc2_setup_standard_order(h1) != MINC2_SUCCESS ||
        minc2_setup_standard_order(h2) != MINC2_SUCCESS) {
        std::cerr << "Failed to setup standard order\n";
        return 1;
    }
    
    int ndim1, ndim2;
    minc2_ndim(h1, &ndim1);
    minc2_ndim(h2, &ndim2);
    
    int failures = 0;
    
    // === File 1 basic assertions ===
    std::cout << "File 1: " << file1 << "\n";
    std::cout << "  ndim = " << ndim1 << "\n";
    
    if (ndim1 != 3) {
        std::cerr << "FAIL: File 1 ndim expected 3, got " << ndim1 << "\n";
        failures++;
    }
    
    struct minc2_dimension *dims1 = nullptr;
    minc2_get_representation_dimensions(h1, &dims1);
    
    // Check dimensions
    if (dims1[0].length != 193 || dims1[1].length != 229 || dims1[2].length != 193) {
        std::cerr << "FAIL: File 1 dimensions mismatch\n";
        failures++;
    }
    
    // Check steps
    if (std::abs(dims1[0].step - 1.0) > 1e-6 || std::abs(dims1[1].step - 1.0) > 1e-6 || std::abs(dims1[2].step - 1.0) > 1e-6) {
        std::cerr << "FAIL: File 1 steps not all 1.0\n";
        failures++;
    }
    
    // Check start
    if (std::abs(dims1[0].start - (-96.0)) > 1e-6 || 
        std::abs(dims1[1].start - (-132.0)) > 1e-6 || 
        std::abs(dims1[2].start - (-78.0)) > 1e-6) {
        std::cerr << "FAIL: File 1 start values mismatch\n";
        failures++;
    }
    
    std::cout << "File 1: Basic assertions passed\n";
    
    // === File 2 basic assertions ===
    std::cout << "\nFile 2: " << file2 << "\n";
    std::cout << "  ndim = " << ndim2 << "\n";
    
    if (ndim2 != 3) {
        std::cerr << "FAIL: File 2 ndim expected 3, got " << ndim2 << "\n";
        failures++;
    }
    
    struct minc2_dimension *dims2 = nullptr;
    minc2_get_representation_dimensions(h2, &dims2);
    
    // Check dimensions
    if (dims2[0].length != 64 || dims2[1].length != 229 || dims2[2].length != 96) {
        std::cerr << "FAIL: File 2 dimensions mismatch\n";
        failures++;
    }
    
    // Check steps
    if (std::abs(dims2[0].step - 3.0) > 1e-6 || 
        std::abs(dims2[1].step - 1.0) > 1e-6 || 
        std::abs(dims2[2].step - 2.0) > 1e-6) {
        std::cerr << "FAIL: File 2 steps mismatch (expected 3,1,2)\n";
        failures++;
    }
    
    // Check start
    if (std::abs(dims2[0].start - (-95.0)) > 1e-6 || 
        std::abs(dims2[1].start - (-132.0)) > 1e-6 || 
        std::abs(dims2[2].start - (-77.5)) > 1e-6) {
        std::cerr << "FAIL: File 2 start values mismatch\n";
        failures++;
    }
    
    std::cout << "File 2: Basic assertions passed\n";
    
    // === Coordinate transformation test ===
    std::cout << "\n=== Coordinate transformation test ===\n";
    
    // For file1: center voxel
    int center1[3] = {dims1[0].length/2, dims1[1].length/2, dims1[2].length/2};
    std::cout << "File1 center voxel: (" << center1[0] << "," << center1[1] << "," << center1[2] << ")\n";
    
    // Build transformation matrix for file1
    double step1[3], start1[3], dirCos1[3][3];
    for (int i = 0; i < 3; ++i) {
        step1[i] = dims1[i].step;
        start1[i] = dims1[i].start;
        for (int j = 0; j < 3; ++j) {
            dirCos1[i][j] = dims1[i].have_dir_cos ? dims1[i].dir_cos[j] : (i == j ? 1.0 : 0.0);
        }
    }
    
    glm::dmat3 affine1(dirCos1[0][0], dirCos1[0][1], dirCos1[0][2],
                       dirCos1[1][0], dirCos1[1][1], dirCos1[1][2],
                       dirCos1[2][0], dirCos1[2][1], dirCos1[2][2]);
    for (int i = 0; i < 3; ++i) affine1[i] *= glm::dvec3(step1[0], step1[1], step1[2]);
    glm::dvec3 trans1(start1[0], start1[1], start1[2]);
    glm::dmat4 voxelToWorld1(
        glm::dvec4(affine1[0], 0.0),
        glm::dvec4(affine1[1], 0.0),
        glm::dvec4(affine1[2], 0.0),
        glm::dvec4(trans1, 1.0)
    );
    
    glm::dvec4 v1(center1[0], center1[1], center1[2], 1.0);
    glm::dvec4 w1 = voxelToWorld1 * v1;
    std::cout << "  -> world: (" << w1.x << "," << w1.y << "," << w1.z << ")\n";
    
    // Expected world: start + voxel * step
    double expectedWorld1[3] = {
        start1[0] + center1[0] * step1[0],
        start1[1] + center1[1] * step1[1],
        start1[2] + center1[2] * step1[2]
    };
    
    if (std::abs(w1.x - expectedWorld1[0]) > 1e-6 ||
        std::abs(w1.y - expectedWorld1[1]) > 1e-6 ||
        std::abs(w1.z - expectedWorld1[2]) > 1e-6) {
        std::cerr << "FAIL: File1 center voxel->world transformation failed\n";
        failures++;
    }
    
    // For file2: center voxel
    int center2[3] = {dims2[0].length/2, dims2[1].length/2, dims2[2].length/2};
    std::cout << "File2 center voxel: (" << center2[0] << "," << center2[1] << "," << center2[2] << ")\n";
    
    double step2[3], start2[3], dirCos2[3][3];
    for (int i = 0; i < 3; ++i) {
        step2[i] = dims2[i].step;
        start2[i] = dims2[i].start;
        for (int j = 0; j < 3; ++j) {
            dirCos2[i][j] = dims2[i].have_dir_cos ? dims2[i].dir_cos[j] : (i == j ? 1.0 : 0.0);
        }
    }
    
    glm::dmat3 affine2(dirCos2[0][0], dirCos2[0][1], dirCos2[0][2],
                       dirCos2[1][0], dirCos2[1][1], dirCos2[1][2],
                       dirCos2[2][0], dirCos2[2][1], dirCos2[2][2]);
    for (int i = 0; i < 3; ++i) affine2[i] *= glm::dvec3(step2[0], step2[1], step2[2]);
    glm::dvec3 trans2(start2[0], start2[1], start2[2]);
    glm::dmat4 voxelToWorld2(
        glm::dvec4(affine2[0], 0.0),
        glm::dvec4(affine2[1], 0.0),
        glm::dvec4(affine2[2], 0.0),
        glm::dvec4(trans2, 1.0)
    );
    
    glm::dvec4 v2(center2[0], center2[1], center2[2], 1.0);
    glm::dvec4 w2 = voxelToWorld2 * v2;
    std::cout << "  -> world: (" << w2.x << "," << w2.y << "," << w2.z << ")\n";
    
    double expectedWorld2[3] = {
        start2[0] + center2[0] * step2[0],
        start2[1] + center2[1] * step2[1],
        start2[2] + center2[2] * step2[2]
    };
    
    if (std::abs(w2.x - expectedWorld2[0]) > 1e-6 ||
        std::abs(w2.y - expectedWorld2[1]) > 1e-6 ||
        std::abs(w2.z - expectedWorld2[2]) > 1e-6) {
        std::cerr << "FAIL: File2 center voxel->world transformation failed\n";
        failures++;
    }
    
    // === Cross-volume transformation test ===
    std::cout << "\n=== Cross-volume transformation test ===\n";
    
    // What slice index in file2 corresponds to file1's world position?
    glm::dmat4 worldToVoxel2 = glm::inverse(voxelToWorld2);
    glm::dvec4 v2_back = worldToVoxel2 * w1;
    std::cout << "World position from file1 center -> file2 voxel: ("
              << v2_back.x << "," << v2_back.y << "," << v2_back.z << ")\n";
    std::cout << "  rounded: (" << (int)std::round(v2_back.x) << "," 
              << (int)std::round(v2_back.y) << "," << (int)std::round(v2_back.z) << ")\n";
    
    // Expected: file2 voxel = (file1_center_world - file2_start) / file2_step
    double expectedVoxel2[3] = {
        (w1.x - start2[0]) / step2[0],
        (w1.y - start2[1]) / step2[1],
        (w1.z - start2[2]) / step2[2]
    };
    
    if (std::abs(v2_back.x - expectedVoxel2[0]) > 1e-4 ||
        std::abs(v2_back.y - expectedVoxel2[1]) > 1e-4 ||
        std::abs(v2_back.z - expectedVoxel2[2]) > 1e-4) {
        std::cerr << "FAIL: Cross-volume transformation test failed\n";
        failures++;
    }
    
    // Verify round-trip: file2 voxel -> world should match file1 world
    glm::dvec4 v2_forward = voxelToWorld2 * glm::dvec4(std::round(v2_back.x), std::round(v2_back.y), std::round(v2_back.z), 1.0);
    std::cout << "Round-trip: file2 voxel -> world: (" << v2_forward.x << "," << v2_forward.y << "," << v2_forward.z << ")\n";
    
    if (std::abs(v2_forward.x - w1.x) > 1.0 ||  // Allow 1mm tolerance due to rounding
        std::abs(v2_forward.y - w1.y) > 1.0 ||
        std::abs(v2_forward.z - w1.z) > 1.0) {
        std::cerr << "FAIL: Round-trip test failed\n";
        failures++;
    }
    
    minc2_close(h1);
    minc2_close(h2);
    minc2_free(h1);
    minc2_free(h2);
    
    if (failures > 0) {
        std::cerr << failures << " test(s) failed!\n";
        return 1;
    }
    
    std::cout << "\nAll tests passed!\n";
    return 0;
}
