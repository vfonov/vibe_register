#pragma once

#include <vector>
#include <string>
#include <iostream>

class Volume {
public:
    int dimensions[3]; // x, y, z sizes
    std::vector<float> data;
    float min_value = 0.0f;
    float max_value = 1.0f;

    Volume();
    ~Volume();

    bool load(const std::string& filename);
    float get(int x, int y, int z) const;
    void generate_test_data();
};
