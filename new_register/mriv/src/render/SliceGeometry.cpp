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

int sliceCountForView(int viewIndex, const glm::ivec3& dimensions)
{
    switch (viewIndex)
    {
        case 1:  return dimensions.x;
        case 2:  return dimensions.y;
        default: return dimensions.z;
    }
}

int mapSliceIndex(int index, int fromCount, int toCount)
{
    if (fromCount <= 0 || toCount <= 0)
        return 0;

    if (fromCount == toCount)
        return std::clamp(index, 0, toCount - 1);

    // Scaled by the last index rather than the count, so the ends line up:
    // the final slice of one volume maps to the final slice of the other,
    // which is what makes a cursor parked at the top of a stack stay there
    // when the columns have different depths.
    if (fromCount == 1)
        return 0;

    double fraction = static_cast<double>(index) / (fromCount - 1);
    int mapped = static_cast<int>(std::lround(fraction * (toCount - 1)));
    return std::clamp(mapped, 0, toCount - 1);
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
