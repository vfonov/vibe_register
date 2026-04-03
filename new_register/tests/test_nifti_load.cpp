#include <iostream>
#include <string>
#include "NiftiVolume.h"
#include "Volume.h"

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <nifti_file>\n";
        return 1;
    }
    
    std::string filename = argv[1];
    Volume vol;
    
    try {
        vol.load(filename);
        
        std::cout << "Successfully loaded NIfTI file: " << filename << "\n";
        std::cout << "Dimensions: " << vol.dimensions.x << " x " 
                  << vol.dimensions.y << " x " << vol.dimensions.z << "\n";
        std::cout << "Step (mm): " << vol.step.x << ", " << vol.step.y << ", " << vol.step.z << "\n";
        std::cout << "Start (mm): " << vol.start.x << ", " << vol.start.y << ", " << vol.start.z << "\n";
        std::cout << "Data range: [" << vol.min_value << ", " << vol.max_value << "]\n";
        
        // Test corner world coordinates
        glm::dvec3 corner_world;
        vol.transformVoxelToWorld(glm::ivec3(0, 0, 0), corner_world);
        std::cout << "Corner (0,0,0) world: (" << corner_world.x << ", " 
                  << corner_world.y << ", " << corner_world.z << ")\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
