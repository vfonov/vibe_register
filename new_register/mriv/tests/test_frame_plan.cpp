/// test_frame_plan.cpp — planFrame()'s showCrosshair switch.
///
/// planFrame() is the single place both the one-shot path and the
/// interactive loop turn a ViewState into a FrameRequest (mriv/PLAN.md).
/// The one-shot path has no interactive cursor and must stay a clean,
/// unannotated image for scripting, so showCrosshair defaults to off and
/// only the interactive call site (cli/Run.cpp) opts in.

#include <cassert>

#include "interactive/FramePlan.hpp"

#include "Volume.h"

using namespace mriv::term;

namespace
{

ViewState makeState()
{
    glm::ivec3 dims(20, 20, 20);
    glm::ivec3 cursor(10, 10, 10);
    return ViewState({dims}, {0}, 'z', cursor, {VolumeDisplay{}});
}

Volume makeVolume(int nx, int ny, int nz, float value)
{
    Volume vol;
    vol.dimensions   = glm::ivec3(nx, ny, nz);
    vol.step         = glm::dvec3(1.0);
    vol.start        = glm::dvec3(0.0);
    vol.dirCos       = glm::dmat3(1.0);
    vol.voxelToWorld = glm::dmat4(1.0);
    vol.worldToVoxel = glm::dmat4(1.0);
    vol.data.assign(static_cast<size_t>(nx) * ny * nz, value);
    vol.min_value = 0.0f;
    vol.max_value = 1.0f;
    return vol;
}

void testPlanFrameOmitsCrosshairsByDefault()
{
    auto state = makeState();
    Volume vol = makeVolume(20, 20, 20, 1.0f);

    auto request = planFrame(state, {&vol}, 200, 200, 1);

    assert(request.panes.size() == 1);
    assert(request.panes[0].crosshairs.empty());
}

void testPlanFrameWiresCrosshairsWhenRequested()
{
    auto state = makeState();
    Volume vol = makeVolume(20, 20, 20, 1.0f);

    auto request = planFrame(state, {&vol}, 200, 200, 1, /*showCrosshair=*/true);

    assert(request.panes.size() == 1);
    assert(request.panes[0].crosshairs.size() == 1);

    auto expected = state.crosshairFor(0, 0); // views() == {0}
    assert(request.panes[0].crosshairs[0].x == expected.u);
    assert(request.panes[0].crosshairs[0].y == expected.v);
}

} // namespace

int main()
{
    testPlanFrameOmitsCrosshairsByDefault();
    testPlanFrameWiresCrosshairsWhenRequested();
    return 0;
}
