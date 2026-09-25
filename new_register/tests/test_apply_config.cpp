/// test_apply_config.cpp — AppState::applyConfig() sanitising view settings.
///
/// Covers: zoom values from a config file are kept within the range the UI
/// allows (zoom 0 divides by zero in the view code), and non-finite pan
/// values fall back to the centre.

#include <cmath>
#include <limits>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "AppConfig.h"
#include "AppState.h"
#include "Volume.h"

namespace
{

/// Give `state` one 4x4x2 volume named a.mnc (AppState is not movable).
void addOneVolume(AppState& state)
{
    Volume vol;
    vol.dimensions = {4, 4, 2};
    vol.data.assign(4 * 4 * 2, 1.0f);

    state.volumes_.push_back(std::move(vol));
    state.volumePaths_.push_back("a.mnc");
    state.volumeNames_.push_back("a.mnc");
    state.initializeViewStates();
}

AppConfig configWithView(double zoom, double pan)
{
    AppConfig cfg;
    VolumeConfig vc;
    vc.path = "a.mnc";
    vc.zoom = {zoom, zoom, zoom};
    vc.panU = {pan, pan, pan};
    vc.panV = {pan, pan, pan};
    cfg.volumes.push_back(vc);
    return cfg;
}

}  // namespace

TEST_CASE("Config zoom is clamped to the UI range", "[config][zoom]")
{
    const double zoom = GENERATE(0.0, -2.0, 1e-9, 1e9,
                                 std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::infinity());
    INFO("zoom " << zoom);
    AppState state;
    addOneVolume(state);
    state.applyConfig(configWithView(zoom, 0.5), 800, 600);

    for (int v = 0; v < 3; ++v)
    {
        const double z = state.viewStates_[0].zoom[v];
        CHECK(std::isfinite(z));
        CHECK(z >= kMinZoom);
        CHECK(z <= kMaxZoom);
    }
}

TEST_CASE("A valid config zoom is kept", "[config][zoom]")
{
    AppState state;
    addOneVolume(state);
    state.applyConfig(configWithView(2.5, 0.25), 800, 600);
    CHECK(state.viewStates_[0].zoom[1] == 2.5);
    CHECK(state.viewStates_[0].panU[1] == 0.25);
}

TEST_CASE("Non-finite config pan falls back to the centre", "[config][pan]")
{
    AppState state;
    addOneVolume(state);
    state.applyConfig(configWithView(1.0, std::numeric_limits<double>::quiet_NaN()), 800, 600);
    for (int v = 0; v < 3; ++v)
    {
        CHECK(state.viewStates_[0].panU[v] == 0.5);
        CHECK(state.viewStates_[0].panV[v] == 0.5);
    }
}
