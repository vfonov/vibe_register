#include <iostream>
#include <minc2-simple.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

int main() {
    const char* file1 = "/app/test_data/mni_icbm152_t1_tal_nlin_sym_09c.mnc";
    const char* file2 = "/app/test_data/mni_icbm152_t1_tal_nlin_sym_09c_thick_slices.mnc";
    
    minc2_file_handle h1 = nullptr, h2 = nullptr;
    
    minc2_allocate(&h1);
    minc2_allocate(&h2);
    
    minc2_open(h1, file1);
    minc2_open(h2, file2);
    
    minc2_setup_standard_order(h1);
    minc2_setup_standard_order(h2);
    
    struct minc2_dimension *dims1 = nullptr, *dims2 = nullptr;
    minc2_get_representation_dimensions(h1, &dims1);
    minc2_get_representation_dimensions(h2, &dims2);
    
    std::cout << "=== File 1: mni_icbm152_t1_tal_nlin_sym_09c.mnc ===\n";
    for (int i = 0; i < 3; ++i) {
        std::cout << "dim[" << i << "]: id=" << dims1[i].id 
                  << " length=" << dims1[i].length
                  << " step=" << dims1[i].step
                  << " start=" << dims1[i].start;
        if (dims1[i].have_dir_cos) {
            std::cout << " dir_cos=(" << dims1[i].dir_cos[0] << "," 
                      << dims1[i].dir_cos[1] << "," << dims1[i].dir_cos[2] << ")";
        }
        std::cout << "\n";
    }
    
    std::cout << "\n=== File 2: mni_icbm152_t1_tal_nlin_sym_09c_thick_slices.mnc ===\n";
    for (int i = 0; i < 3; ++i) {
        std::cout << "dim[" << i << "]: id=" << dims2[i].id 
                  << " length=" << dims2[i].length
                  << " step=" << dims2[i].step
                  << " start=" << dims2[i].start;
        if (dims2[i].have_dir_cos) {
            std::cout << " dir_cos=(" << dims2[i].dir_cos[0] << "," 
                      << dims2[i].dir_cos[1] << "," << dims2[i].dir_cos[2] << ")";
        }
        std::cout << "\n";
    }
    
    // Test world (0,0,0) -> voxel manually
    std::cout << "\n=== Manual calculation for world (0,0,0) ===\n";
    
    // For file 1: start = (-96, -132, -78), step = (1, 1, 1)
    // voxel = (world - start) / step = (0 - (-96), 0 - (-132), 0 - (-78)) / (1,1,1) = (96, 132, 78)
    std::cout << "File 1 expected: (96, 132, 78)\n";
    
    // For file 2: start = (-95, -132, -77.5), step = (3, 1, 2)
    // voxel = (world - start) / step = (0 - (-95), 0 - (-132), 0 - (-77.5)) / (3,1,2) = (95/3, 132, 77.5/2)
    //        = (31.666..., 132, 38.75)
    std::cout << "File 2 expected: (31.666..., 132, 38.75)\n";
    
    std::cout << "\nExpected (from user):\n";
    std::cout << "File 1: world (0,0,0) -> voxel (78, 132, 96)\n";
    std::cout << "File 2: world (0,0,0) -> voxel (38.75, 132, 31.666...)\n";
    
    std::cout << "\nNote: The expected values from user suggest dimension ordering is ZXY, not XYZ!\n";
    
    minc2_close(h1);
    minc2_close(h2);
    minc2_free(h1);
    minc2_free(h2);
    
    return 0;
}
