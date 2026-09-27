#include "render/SliceGeometry.hpp"

#include <algorithm>
#include <cmath>

namespace mriv::term
{

std::optional<int> viewIndexForAxis(char axis)
{
    switch (axis)
    {
        case 'z': return 0;
        case 'x': return 1;
        case 'y': return 2;
        default:  return std::nullopt;
    }
}

char axisForViewIndex(int viewIndex)
{
    switch (viewIndex)
    {
        case 1:  return 'x';
        case 2:  return 'y';
        default: return 'z';
    }
}

int sliceCountForView(int viewIndex, const glm::ivec3& dimensions)
{
    switch (viewIndex)
    {
        case 1:  return dimensions.x;
        case 2:  return dimensions.y;
        default: return dimensions.z;
    }
}

glm::ivec3 mapVoxelThroughWorld(const glm::ivec3& voxel,
                                const VolumeGeometry& from,
                                const VolumeGeometry& to)
{
    glm::dvec4 world = from.voxelToWorld * glm::dvec4(glm::dvec3(voxel), 1.0);
    glm::dvec4 mapped = to.worldToVoxel * world;

    glm::ivec3 result;
    for (int axis = 0; axis < 3; ++axis)
    {
        int nearest = static_cast<int>(std::lround(mapped[axis]));
        result[axis] = std::clamp(nearest, 0, std::max(to.dimensions[axis] - 1, 0));
    }
    return result;
}

AspectAxes aspectAxesForView(int viewIndex)
{
    switch (viewIndex)
    {
        case 0:  return AspectAxes{0, 1};
        case 1:  return AspectAxes{1, 2};
        case 2:  return AspectAxes{0, 2};
        default: return AspectAxes{0, 1};
    }
}

} // namespace mriv::term
