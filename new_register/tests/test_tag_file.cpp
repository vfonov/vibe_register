#include "TagFile.h"
#include <iostream>
#include <fstream>
#include <cmath>

int main() {
    try {
        TagFile tagFile;
        tagFile.load("/app/test_data/tags.tag");
        
        auto& points = tagFile.getTagPoints();
        int volCount = tagFile.getVolumeCount();
        
        std::cerr << "Volume count: " << volCount << "\n";
        std::cerr << "Number of tag points: " << points.size() << "\n\n";
        
        // Expected values from tags.tag
        std::vector<glm::dvec3> expected = {
            glm::dvec3(-30.1075706481934, 30.6739044189453, 18),
            glm::dvec3(23.0824699401855, 27.6631469726562, 18),
            glm::dvec3(-22.0788841247559, -44.5950202941895, 18)
        };
        
        const double tolerance = 1e-6;
        bool allMatch = true;
        
        for (size_t i = 0; i < points.size(); ++i) {
            std::cerr << "Tag " << i << ":\n";
            std::cerr << "  Position: (" << points[i].position.x << ", "
                      << points[i].position.y << ", "
                      << points[i].position.z << ")\n";
            std::cerr << "  Label: \"" << points[i].label << "\"\n";
            
            // Check position
            double diff = std::sqrt(std::pow(points[i].position.x - expected[i].x, 2) +
                                    std::pow(points[i].position.y - expected[i].y, 2) +
                                    std::pow(points[i].position.z - expected[i].z, 2));
            
            if (diff > tolerance) {
                std::cerr << "  MISMATCH! Expected: (" << expected[i].x << ", "
                          << expected[i].y << ", " << expected[i].z << ")\n";
                allMatch = false;
            } else {
                std::cerr << "  Match: YES\n";
            }
            std::cerr << "\n";
        }
        
        if (allMatch && points.size() == 3 && volCount == 1) {
            std::cerr << "\nTest PASSED!\n";
            return 0;
        } else {
            std::cerr << "\nTest FAILED!\n";
            return 1;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
}
