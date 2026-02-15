#include <iostream>
#include <minc2-simple.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

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
    
    std::cout << "File 1: " << file1 << "\n";
    std::cout << "  ndim = " << ndim1 << "\n";
    
    struct minc2_dimension *dims1 = nullptr;
    minc2_get_representation_dimensions(h1, &dims1);
    
    std::cout << "  Dimensions:\n";
    for (int i = 0; i < ndim1; ++i) {
        std::cout << "    [" << i << "] id=" << dims1[i].id 
                  << " length=" << dims1[i].length
                  << " step=" << dims1[i].step
                  << " start=" << dims1[i].start;
        if (dims1[i].have_dir_cos) {
            std::cout << " dir_cos=(" << dims1[i].dir_cos[0] << "," 
                      << dims1[i].dir_cos[1] << "," << dims1[i].dir_cos[2] << ")";
        }
        std::cout << "\n";
    }
    
    std::cout << "\nFile 2: " << file2 << "\n";
    std::cout << "  ndim = " << ndim2 << "\n";
    
    struct minc2_dimension *dims2 = nullptr;
    minc2_get_representation_dimensions(h2, &dims2);
    
    std::cout << "  Dimensions:\n";
    for (int i = 0; i < ndim2; ++i) {
        std::cout << "    [" << i << "] id=" << dims2[i].id 
                  << " length=" << dims2[i].length
                  << " step=" << dims2[i].step
                  << " start=" << dims2[i].start;
        if (dims2[i].have_dir_cos) {
            std::cout << " dir_cos=(" << dims2[i].dir_cos[0] << "," 
                      << dims2[i].dir_cos[1] << "," << dims2[i].dir_cos[2] << ")";
        }
        std::cout << "\n";
    }
    
    // Test coordinate transformation
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
    
    // Now test: what slice index in file2 corresponds to file1's world position?
    glm::dmat4 worldToVoxel2 = glm::inverse(voxelToWorld2);
    glm::dvec4 v2_back = worldToVoxel2 * w1;
    std::cout << "\nWorld position from file1 center -> file2 voxel: ("
              << v2_back.x << "," << v2_back.y << "," << v2_back.z << ")\n";
    std::cout << "  rounded: (" << (int)std::round(v2_back.x) << "," 
              << (int)std::round(v2_back.y) << "," << (int)std::round(v2_back.z) << ")\n";
    
    minc2_close(h1);
    minc2_close(h2);
    minc2_free(h1);
    minc2_free(h2);
    
    return 0;
}
