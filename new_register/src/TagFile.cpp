#include "TagFile.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

void TagFile::load(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open tag file: " + filename);
    }
    
    tagPoints_.clear();
    volumeCount_ = 1;
    
    std::string line;
    bool inPointsSection = false;
    
    while (std::getline(file, line)) {
        // Trim whitespace
        auto start = line.find_first_not_of(" \t\r\n");
        auto end = line.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start, end - start + 1);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '%') continue;
        
        // Check for header
        if (line.find("MNI Tag Point File") != std::string::npos) {
            continue;
        }
        
        // Check for volume count
        if (line.find("Volumes =") != std::string::npos) {
            size_t pos = line.find("Volumes =");
            size_t valStart = line.find_first_of("0123456789", pos);
            if (valStart != std::string::npos) {
                volumeCount_ = std::stoi(line.substr(valStart));
            }
            continue;
        }
        
        // Check for points section start
        if (line.find("Points =") != std::string::npos) {
            inPointsSection = true;
            continue;
        }
        
        // Check for points section end
        if (line == ";") {
            inPointsSection = false;
            continue;
        }
        
        // Parse tag point data
        if (inPointsSection) {
            // Remove trailing semicolon if present
            if (!line.empty() && line.back() == ';') {
                line = line.substr(0, line.length() - 1);
            }
            
            std::vector<double> values;
            std::istringstream iss(line);
            double val;
            while (iss >> val) {
                values.push_back(val);
            }
            
            if (values.size() >= 3) {
                TagPoint point;
                point.position = glm::dvec3(values[0], values[1], values[2]);
                point.label = "";
                
                // Try to extract label if present (quoted string at the end)
                size_t labelPos = line.find('"');
                if (labelPos != std::string::npos) {
                    size_t labelEnd = line.find('"', labelPos + 1);
                    if (labelEnd != std::string::npos) {
                        point.label = line.substr(labelPos + 1, labelEnd - labelPos - 1);
                    }
                }
                
                tagPoints_.push_back(point);
            }
        }
    }
    
    if (!file.good() && !file.eof()) {
        throw std::runtime_error("Error reading tag file: " + filename);
    }
}
