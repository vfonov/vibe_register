// Test loading of .tag file using TagWrapper class
#include "TagWrapper.hpp"
#include <iostream>
#include <glm/vec3.hpp>
#include <cmath>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <tag_file>\n";
        return 1;
    }
    const char* tagPath = argv[1];

    TagWrapper wrapper;
    try {
        wrapper.load(tagPath);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load tag file: " << e.what() << "\n";
        return 1;
    }

    // Verify expected number of tag points (3 for our test file)
    if (wrapper.points().size() != 3) {
        std::cerr << "Unexpected number of tag points: " << wrapper.points().size() << " (expected 3)\n";
        return 1;
    }

    // Expected coordinates (from test_data/mni_icbm152_t1_tal_nlin_sym_09c.tag)
    const double expected[3][3] = {
        { -30.1075706481934, 30.6739044189453, 18.0 },
        { 23.0824699401855, 27.6631469726562, 18.0 },
        { -22.0788841247559, -44.5950202941895, 18.0 }
    };

    const double tol = 1e-6;
    const auto& pts = wrapper.points();
    for (size_t i = 0; i < pts.size(); ++i) {
        if (std::fabs(pts[i].x - expected[i][0]) > tol ||
            std::fabs(pts[i].y - expected[i][1]) > tol ||
            std::fabs(pts[i].z - expected[i][2]) > tol) {
            std::cerr << "Tag point " << i << " mismatch: (" << pts[i].x << "," << pts[i].y << "," << pts[i].z << ")\n";
            return 1;
        }
    }

    std::cerr << "Tag file loaded and verified successfully.\n";
    return 0;
}
