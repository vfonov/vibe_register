/// test_qc_row_switch.cpp — display settings carried across a QC row switch.
///
/// Interface::switchQCRow() snapshots every view's display settings, loads
/// the next row's volumes (which resets the view states), then re-applies the
/// snapshot. The log transform and colour-map inversion were left out of the
/// snapshot, so they silently turned off on every row change.

#include <catch2/catch_test_macros.hpp>

#include "AppState.h"
#include "Volume.h"

namespace
{

/// Replace the volumes in `state` with one volume of the given dims, the way
/// loadVolumeSet() does (view states are re-initialised).
void loadRow(AppState& state, glm::ivec3 dims, float scale)
{
    state.clearAllVolumes();
    Volume vol;
    vol.dimensions = dims;
    vol.data.assign(static_cast<size_t>(dims.x) * dims.y * dims.z, 1.0f);
    vol.data[0] = scale;
    vol.min_value = 1.0;
    vol.max_value = scale;
    state.volumes_.push_back(std::move(vol));
    state.volumePaths_.push_back("row.mnc");
    state.volumeNames_.push_back("row.mnc");
    state.initializeViewStates();
}

}  // namespace

TEST_CASE("Log transform and inversion survive a QC row switch", "[qc][rowswitch]")
{
    AppState state;
    loadRow(state, {6, 6, 6}, 10.0f);
    VolumeViewState& before = state.viewStates_[0];
    before.useLogTransform = true;
    before.invertColourMap = true;
    before.colourMap = ColourMapType::HotMetal;

    const auto saved = state.captureDisplaySettings();
    loadRow(state, {6, 6, 6}, 100.0f);
    state.restoreDisplaySettings(saved);

    const VolumeViewState& after = state.viewStates_[0];
    CHECK(after.colourMap == ColourMapType::HotMetal);
    CHECK(after.useLogTransform);
    CHECK(after.invertColourMap);
}

TEST_CASE("Slice indices are clamped to the next row's dimensions", "[qc][rowswitch]")
{
    AppState state;
    loadRow(state, {8, 8, 8}, 10.0f);
    state.viewStates_[0].sliceIndices = {7, 6, 5};

    const auto saved = state.captureDisplaySettings();
    loadRow(state, {4, 4, 4}, 10.0f);
    state.restoreDisplaySettings(saved);

    CHECK(state.viewStates_[0].sliceIndices == glm::ivec3(3, 3, 3));
}
