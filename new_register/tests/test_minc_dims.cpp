#include <iostream>
#include <minc2-simple.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <cmath>

int main() {
    const char* file1 = "/app/test_data/mni_icbm152_t1_tal_nlin_sym_09c.mnc";
    const char* file2 = "/app/test_data/mni_icbm152_t1_tal_nlin_sym_09c_thick_slices.mnc";
    
    minc2_file_handle h1 = nullptr, h2 = nullptr;
    
    minc2_allocate(&h1);
    minc2_allocate(&h2);
    
    if (minc2_open(h1, file1) != MINC2_SUCCESS) {
        std::cerr << "Failed to open " << file1 << "\n";
        return 1;
    }
    if (minc2_open(h2, file2) != MINC2_SUCCESS) {
        std::cerr << "Failed to open " << file2 << "\n";
        return 1;
    }
    
    minc2_setup_standard_order(h1);
    minc2_setup_standard_order(h2);
    
    struct minc2_dimension *dims1 = nullptr, *dims2 = nullptr;
    minc2_get_representation_dimensions(h1, &dims1);
    minc2_get_representation_dimensions(h2, &dims2);
    
    int failures = 0;
    
    // === File 1 assertions ===
    std::cout << "=== File 1: mni_icbm152_t1_tal_nlin_sym_09c.mnc ===\n";
    
    // Check dimension IDs: X=1, Y=2, Z=3
    if (dims1[0].id != 1) { std::cerr << "FAIL: dim[0] id expected 1, got " << dims1[0].id << "\n"; failures++; }
    if (dims1[1].id != 2) { std::cerr << "FAIL: dim[1] id expected 2, got " << dims1[1].id << "\n"; failures++; }
    if (dims1[2].id != 3) { std::cerr << "FAIL: dim[2] id expected 3, got " << dims1[2].id << "\n"; failures++; }
    
    // Check lengths
    if (dims1[0].length != 193) { std::cerr << "FAIL: dim[0] length expected 193, got " << dims1[0].length << "\n"; failures++; }
    if (dims1[1].length != 229) { std::cerr << "FAIL: dim[1] length expected 229, got " << dims1[1].length << "\n"; failures++; }
    if (dims1[2].length != 193) { std::cerr << "FAIL: dim[2] length expected 193, got " << dims1[2].length << "\n"; failures++; }
    
    // Check steps: X=1, Y=1, Z=1
    if (std::abs(dims1[0].step - 1.0) > 1e-6) { std::cerr << "FAIL: dim[0] step expected 1, got " << dims1[0].step << "\n"; failures++; }
    if (std::abs(dims1[1].step - 1.0) > 1e-6) { std::cerr << "FAIL: dim[1] step expected 1, got " << dims1[1].step << "\n"; failures++; }
    if (std::abs(dims1[2].step - 1.0) > 1e-6) { std::cerr << "FAIL: dim[2] step expected 1, got " << dims1[2].step << "\n"; failures++; }
    
    // Check start: X=-96, Y=-132, Z=-78
    if (std::abs(dims1[0].start - (-96.0)) > 1e-6) { std::cerr << "FAIL: dim[0] start expected -96, got " << dims1[0].start << "\n"; failures++; }
    if (std::abs(dims1[1].start - (-132.0)) > 1e-6) { std::cerr << "FAIL: dim[1] start expected -132, got " << dims1[1].start << "\n"; failures++; }
    if (std::abs(dims1[2].start - (-78.0)) > 1e-6) { std::cerr << "FAIL: dim[2] start expected -78, got " << dims1[2].start << "\n"; failures++; }
    
    std::cout << "File 1: All assertions passed\n";
    
    // === File 2 assertions ===
    std::cout << "\n=== File 2: mni_icbm152_t1_tal_nlin_sym_09c_thick_slices.mnc ===\n";
    
    // Check dimension IDs
    if (dims2[0].id != 1) { std::cerr << "FAIL: dim[0] id expected 1, got " << dims2[0].id << "\n"; failures++; }
    if (dims2[1].id != 2) { std::cerr << "FAIL: dim[1] id expected 2, got " << dims2[1].id << "\n"; failures++; }
    if (dims2[2].id != 3) { std::cerr << "FAIL: dim[2] id expected 3, got " << dims2[2].id << "\n"; failures++; }
    
    // Check lengths
    if (dims2[0].length != 64) { std::cerr << "FAIL: dim[0] length expected 64, got " << dims2[0].length << "\n"; failures++; }
    if (dims2[1].length != 229) { std::cerr << "FAIL: dim[1] length expected 229, got " << dims2[1].length << "\n"; failures++; }
    if (dims2[2].length != 96) { std::cerr << "FAIL: dim[2] length expected 96, got " << dims2[2].length << "\n"; failures++; }
    
    // Check steps: X=3, Y=1, Z=2
    if (std::abs(dims2[0].step - 3.0) > 1e-6) { std::cerr << "FAIL: dim[0] step expected 3, got " << dims2[0].step << "\n"; failures++; }
    if (std::abs(dims2[1].step - 1.0) > 1e-6) { std::cerr << "FAIL: dim[1] step expected 1, got " << dims2[1].step << "\n"; failures++; }
    if (std::abs(dims2[2].step - 2.0) > 1e-6) { std::cerr << "FAIL: dim[2] step expected 2, got " << dims2[2].step << "\n"; failures++; }
    
    // Check start: X=-95, Y=-132, Z=-77.5
    if (std::abs(dims2[0].start - (-95.0)) > 1e-6) { std::cerr << "FAIL: dim[0] start expected -95, got " << dims2[0].start << "\n"; failures++; }
    if (std::abs(dims2[1].start - (-132.0)) > 1e-6) { std::cerr << "FAIL: dim[1] start expected -132, got " << dims2[1].start << "\n"; failures++; }
    if (std::abs(dims2[2].start - (-77.5)) > 1e-6) { std::cerr << "FAIL: dim[2] start expected -77.5, got " << dims2[2].start << "\n"; failures++; }
    
    std::cout << "File 2: All assertions passed\n";
    
    // === Test world (0,0,0) -> voxel transformation ===
    std::cout << "\n=== Test: world (0,0,0) -> voxel ===\n";
    
    // For file 1: start = (-96, -132, -78), step = (1, 1, 1)
    // voxel = (world - start) / step = (0 - (-96), 0 - (-132), 0 - (-78)) / (1,1,1) = (96, 132, 78)
    int expected1[3] = {96, 132, 78};
    int actual1[3];
    actual1[0] = static_cast<int>(std::round((0.0 - dims1[0].start) / dims1[0].step));
    actual1[1] = static_cast<int>(std::round((0.0 - dims1[1].start) / dims1[1].step));
    actual1[2] = static_cast<int>(std::round((0.0 - dims1[2].start) / dims1[2].step));
    
    std::cout << "File 1: world (0,0,0) -> voxel (" << actual1[0] << "," << actual1[1] << "," << actual1[2] << ")\n";
    std::cout << "Expected: (" << expected1[0] << "," << expected1[1] << "," << expected1[2] << ")\n";
    
    if (actual1[0] != expected1[0] || actual1[1] != expected1[1] || actual1[2] != expected1[2]) {
        std::cerr << "FAIL: File 1 world->voxel transformation failed\n";
        failures++;
    }
    
    // For file 2: start = (-95, -132, -77.5), step = (3, 1, 2)
    // voxel = (world - start) / step = (0 - (-95), 0 - (-132), 0 - (-77.5)) / (3,1,2) = (31.666..., 132, 38.75)
    double expected2[3] = {31.6666666667, 132.0, 38.75};
    double actual2[3];
    actual2[0] = (0.0 - dims2[0].start) / dims2[0].step;
    actual2[1] = (0.0 - dims2[1].start) / dims2[1].step;
    actual2[2] = (0.0 - dims2[2].start) / dims2[2].step;
    
    std::cout << "File 2: world (0,0,0) -> voxel (" << actual2[0] << "," << actual2[1] << "," << actual2[2] << ")\n";
    std::cout << "Expected: (" << expected2[0] << "," << expected2[1] << "," << expected2[2] << ")\n";
    
    if (std::abs(actual2[0] - expected2[0]) > 1e-6 || 
        std::abs(actual2[1] - expected2[1]) > 1e-6 || 
        std::abs(actual2[2] - expected2[2]) > 1e-6) {
        std::cerr << "FAIL: File 2 world->voxel transformation failed\n";
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
