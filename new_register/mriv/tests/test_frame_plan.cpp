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
    return ViewState({VolumeGeometry{dims}}, {0}, 'z', cursor, {VolumeDisplay{}});
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

/// The overlay column is the state's decision: planFrame() only carries it
/// into the request. Off unless 'o' turned it on, so the one-shot path --
/// which never presses a key -- cannot grow a column.
void testPlanFrameCarriesTheOverlaySwitch()
{
    glm::ivec3 dims(20, 20, 20);
    ViewState state({VolumeGeometry{dims}, VolumeGeometry{dims}}, {0}, 'z',
                    glm::ivec3(10, 10, 10), {VolumeDisplay{}, VolumeDisplay{}});
    Volume a = makeVolume(20, 20, 20, 0.2f);
    Volume b = makeVolume(20, 20, 20, 0.8f);

    assert(!planFrame(state, {&a, &b}, 200, 200, 1).showOverlay);

    state.handleKey('o');
    auto request = planFrame(state, {&a, &b}, 200, 200, 1);
    assert(request.showOverlay);
    assert(request.panes.size() == 2); // the overlay is not a pane
}

/// The zoom and what it is centred on are the state's: planFrame() carries
/// both. The focus is filled whether or not a crosshair is drawn, so
/// one-shot output zooms onto the same place without gaining a mark.
void testPlanFrameCarriesTheZoomAndItsFocus()
{
    auto state = makeState();
    Volume vol = makeVolume(20, 20, 20, 1.0f);

    assert(planFrame(state, {&vol}, 200, 200, 1).zoom == 1.0);

    state.setZoom(2.5);
    state.handleKey('j'); // somewhere other than where it started

    auto request = planFrame(state, {&vol}, 200, 200, 1);
    assert(request.zoom == 2.5);
    assert(request.panes[0].crosshairs.empty());
    assert(request.panes[0].focus.size() == 1);

    auto expected = state.crosshairFor(0, 0);
    assert(request.panes[0].focus[0].x == expected.u);
    assert(request.panes[0].focus[0].y == expected.v);
}

} // namespace

int main()
{
    testPlanFrameCarriesTheOverlaySwitch();
    testPlanFrameOmitsCrosshairsByDefault();
    testPlanFrameWiresCrosshairsWhenRequested();
    testPlanFrameCarriesTheZoomAndItsFocus();
    return 0;
}
