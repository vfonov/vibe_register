// Test loading of .tag file using minc2-simple API
#include "minc2-simple.h"
#include <iostream>
#include <cmath>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <tag_file>\n";
        return 1;
    }
    const char* tagPath = argv[1];

    // Allocate tag handle
    minc2_tags_handle tags = minc2_tags_allocate0();
    if (!tags) {
        std::cerr << "Failed to allocate tag handle\n";
        return 1;
    }

    // Load tag file
    int status = minc2_tags_load(tags, tagPath);
    if (status != MINC2_SUCCESS) {
        std::cerr << "Failed to load tag file: status=" << status << "\n";
        minc2_tags_free(tags);
        return 1;
    }

    // Verify expected number of tag points (3 for our test file)
    if (tags->n_tag_points != 3) {
        std::cerr << "Unexpected number of tag points: " << tags->n_tag_points << " (expected 3)\n";
        minc2_tags_free(tags);
        return 1;
    }

    // Expected coordinates (from test_data/mni_icbm152_t1_tal_nlin_sym_09c.tag)
    const double expected[3][3] = {
        { -30.1075706481934, 30.6739044189453, 18.0 },
        { 23.0824699401855, 27.6631469726562, 18.0 },
        { -22.0788841247559, -44.5950202941895, 18.0 }
    };

    const double tol = 1e-6;
    for (int i = 0; i < 3; ++i) {
        double x = tags->tags_volume1[i*3+0];
        double y = tags->tags_volume1[i*3+1];
        double z = tags->tags_volume1[i*3+2];
        if (std::fabs(x - expected[i][0]) > tol ||
            std::fabs(y - expected[i][1]) > tol ||
            std::fabs(z - expected[i][2]) > tol) {
            std::cerr << "Tag point " << i << " mismatch: (" << x << "," << y << "," << z << ")\n";
            minc2_tags_free(tags);
            return 1;
        }
    }

    std::cerr << "Tag file loaded and verified successfully.\n";
    minc2_tags_free(tags);
    return 0;
}
