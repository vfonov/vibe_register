#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>
#include <cstdint>

int main() {
    int failures = 0;

    std::cout << "=== Label value rounding test ===\n";

    std::vector<float> testValues = {
        0.0f, 0.4f, 0.5f, 0.6f, 1.0f,
        1.4f, 1.5f, 1.6f, 2.0f, 2.1f,
        2.4f, 2.5f, 2.6f, 2.9f, 3.0f,
        -0.4f, -0.5f, -0.6f, -1.4f, -1.5f, -1.6f
    };

    std::vector<int> expectedRounded = {
        0, 0, 1, 1, 1,
        1, 2, 2, 2, 2,
        2, 3, 3, 3, 3,
        0, -1, -1, -1, -2, -2
    };

    for (size_t i = 0; i < testValues.size(); ++i) {
        float val = testValues[i];
        int expected = expectedRounded[i];
        int actual = static_cast<int>(std::round(val));
        
        if (actual != expected) {
            std::cerr << "FAIL: std::round(" << val << ") = " << actual 
                      << ", expected " << expected << "\n";
            failures++;
        } else {
            std::cout << "  PASS: std::round(" << val << ") = " << actual << "\n";
        }
    }

    std::cout << "\n=== Verify code changes ===\n";
    
    std::vector<float> labelValues = {1.4f, 1.5f, 1.6f};
    for (float val : labelValues) {
        int labelId = static_cast<int>(std::round(val));
        std::cout << "  Label value " << val << " -> label ID " << labelId << "\n";
    }

    if (failures > 0) {
        std::cerr << "\n" << failures << " test(s) failed!\n";
        return 1;
    }

    std::cout << "\nAll tests passed!\n";
    return 0;
}
