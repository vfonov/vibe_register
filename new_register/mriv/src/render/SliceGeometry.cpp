#include "render/SliceGeometry.hpp"

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

int sliceCountForView(int viewIndex, const glm::ivec3& dimensions)
{
    switch (viewIndex)
    {
        case 1:  return dimensions.x;
        case 2:  return dimensions.y;
        default: return dimensions.z;
    }
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
