#include "cli/VolumeInfo.hpp"

#include <sstream>

#include "Volume.h"

namespace mriv::term
{

std::string formatVolumeInfo(const Volume& vol, const std::string& path)
{
    std::ostringstream out;
    out << "=== " << path << " ===\n";
    out << "  dimensions: " << vol.dimensions.x << " x " << vol.dimensions.y << " x "
        << vol.dimensions.z << "\n";
    out << "  step  : " << vol.step.x << "  " << vol.step.y << "  " << vol.step.z << "\n";
    out << "  start : " << vol.start.x << "  " << vol.start.y << "  " << vol.start.z << "\n";
    out << "  range : [" << vol.min_value << ", " << vol.max_value << "]\n";

    out << "  dirCos X: " << vol.dirCos[0][0] << "  " << vol.dirCos[0][1] << "  "
        << vol.dirCos[0][2] << "\n";
    out << "  dirCos Y: " << vol.dirCos[1][0] << "  " << vol.dirCos[1][1] << "  "
        << vol.dirCos[1][2] << "\n";
    out << "  dirCos Z: " << vol.dirCos[2][0] << "  " << vol.dirCos[2][1] << "  "
        << vol.dirCos[2][2] << "\n";

    auto w0 = vol.voxelToWorld * glm::dvec4(0, 0, 0, 1);
    auto wm = vol.voxelToWorld
        * glm::dvec4(vol.dimensions.x - 1, vol.dimensions.y - 1, vol.dimensions.z - 1, 1);
    out << "  world(0,0,0)      : " << w0.x << "  " << w0.y << "  " << w0.z << "\n";
    out << "  world(max,max,max): " << wm.x << "  " << wm.y << "  " << wm.z << "\n";

    return out.str();
}

} // namespace mriv::term
