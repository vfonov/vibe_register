#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>

struct TagPoint {
    glm::dvec3 position;
    std::string label;
};

class TagFile {
public:
    void load(const std::string& filename);
    const std::vector<TagPoint>& getTagPoints() const { return tagPoints_; }
    int getVolumeCount() const { return volumeCount_; }
    
private:
    std::vector<TagPoint> tagPoints_;
    int volumeCount_ = 1;
};
