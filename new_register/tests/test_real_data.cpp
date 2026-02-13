#include "Volume.h"
#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <iomanip>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <mnc_file1> [mnc_file2 ...]" << std::endl;
        return 1;
    }

    int failures = 0;

    for (int i = 1; i < argc; ++i) {
        std::string filename = argv[i];
        std::cout << "Testing file: " << filename << " ... ";
        
        Volume vol;
        try
        {
            vol.load(filename);
        }
        catch (const std::exception& e)
        {
            std::cout << "FAILED to load: " << e.what() << std::endl;
            failures++;
            continue;
        }

        if (vol.dimensions[0] <= 0 || vol.dimensions[1] <= 0 || vol.dimensions[2] <= 0) {
            std::cout << "FAILED. Invalid dimensions: " 
                      << vol.dimensions[0] << "x" << vol.dimensions[1] << "x" << vol.dimensions[2] 
                      << std::endl;
            failures++;
            continue;
        }

        size_t expected_size = (size_t)vol.dimensions[0] * (size_t)vol.dimensions[1] * (size_t)vol.dimensions[2];
        if (vol.data.size() != expected_size) {
            std::cout << "FAILED. Data size mismatch. Expected " << expected_size 
                      << ", got " << vol.data.size() << std::endl;
            failures++;
            continue;
        }

        // Calculate mean
        double sum = 0.0;
        for (float v : vol.data) {
            sum += (double)v;
        }
        double mean = sum / (double)vol.data.size();
        
        std::cout << "OK. Dims: " << vol.dimensions[0] << "x" << vol.dimensions[1] << "x" << vol.dimensions[2] 
                  << ", Step: " << vol.step[0] << "x" << vol.step[1] << "x" << vol.step[2]
                  << ", Range: [" << vol.min_value << ", " << vol.max_value << "]";
        std::cout << std::fixed << std::setprecision(8) << ", Mean: " << mean;

        // Verify spatial metadata: steps must be positive (setup_standard_order)
        // and non-zero
        for (int d = 0; d < 3; ++d)
        {
            if (vol.step[d] <= 0.0)
            {
                std::cout << " [FAILED] step[" << d << "] = " << vol.step[d]
                          << " (expected positive)" << std::endl;
                failures++;
                continue;
            }
        }

        // Check against expected values
        double expected_mean = -1.0;
        double tolerance = 1e-5; // Slightly loose tolerance due to float precision

        if (filename.find("mni_icbm152_t1_tal_nlin_sym_09c_mask.mnc") != std::string::npos) {
            expected_mean = 0.2211646372;
        } else if (filename.find("mni_icbm152_t1_tal_nlin_sym_09c.mnc") != std::string::npos) {
            expected_mean = 29.61005195;
        }

        if (expected_mean != -1.0) {
            if (std::abs(mean - expected_mean) > tolerance) {
                 std::cout << " [FAILED] Mean mismatch! Expected " << expected_mean << " but got " << mean << std::endl;
                 failures++;
                 continue;
            } else {
                 std::cout << " [MATCH]";
            }
        }
        std::cout << std::endl;
    }

    return failures > 0 ? 1 : 0;
}
