#include "ViewManager.h"

#include <algorithm>
#include <cmath>

#include "ColourMap.h"
#include "GraphicsBackend.h"
#include "SliceRenderer.h"
#include "Transform.h"
#include "Volume.h"

ViewManager::ViewManager(AppState& state, GraphicsBackend& backend)
    : state_(state), backend_(backend) {}

namespace {

/// SliceRenderer parameters for one view state.
VolumeRenderParams renderParamsOf(const VolumeViewState& st) {
    VolumeRenderParams p;
    p.valueMin = st.valueRange[0];
    p.valueMax = st.valueRange[1];
    p.colourMap = st.colourMap;
    p.overlayAlpha = st.overlayAlpha;
    p.underColourMode = st.underColourMode;
    p.overColourMode = st.overColourMode;
    p.useLogTransform = st.useLogTransform;
    p.invertColourMap = st.invertColourMap;
    return p;
}

/// Slice index along the slicing axis of a view (0=axial Z, 1=sagittal X, 2=coronal Y).
int sliceAlong(const glm::ivec3& indices, int viewIndex) {
    if (viewIndex == 0)
        return indices.z;
    if (viewIndex == 1)
        return indices.x;
    return indices.y;
}

} // namespace

// The kClamp* modes in VolumeViewState are passed straight to SliceRenderer.
static_assert(kClampCurrent == kSliceClampCurrent && kClampTransparent == kSliceClampTransparent &&
              kClampBlack == kSliceClampBlack && kClampYellow == kSliceClampYellow &&
              kClampWhite == kSliceClampWhite && kClampRed == kSliceClampRed &&
              kClampGreen == kSliceClampGreen && kClampBlue == kSliceClampBlue,
              "AppState and SliceRenderer clamp modes must agree");

void ViewManager::uploadTexture(std::unique_ptr<Texture>& tex, const RenderedSlice& slice) {
    const int w = slice.width;
    const int h = slice.height;
    if (!tex) {
        tex = backend_.createTexture(w, h, slice.pixels.data());
    } else if (tex->width != w || tex->height != h) {
        backend_.destroyTexture(tex.get());
        tex = backend_.createTexture(w, h, slice.pixels.data());
    } else {
        backend_.updateTexture(tex.get(), slice.pixels.data());
    }
}

void ViewManager::updateSliceTexture(int volumeIndex, int viewIndex) {
    if (volumeIndex < 0 ||
        volumeIndex >= state_.volumeCount())
        return;
    if (volumeIndex >= static_cast<int>(state_.viewStates_.size()))
        return;

    const Volume& vol = state_.volumes_[volumeIndex];
    if (vol.data.empty())
        return;

    VolumeViewState& state = state_.viewStates_[volumeIndex];
    RenderedSlice slice = renderSlice(vol, renderParamsOf(state), viewIndex,
                                      sliceAlong(state.sliceIndices, viewIndex));
    uploadTexture(state.sliceTextures[viewIndex], slice);
}

void ViewManager::updateOverlayTexture(int viewIndex) {
    int numVols = state_.volumeCount();
    if (numVols < 2)
        return;

    const Volume& ref = state_.volumes_[0];
    if (ref.data.empty())
        return;

    // All volumes are resampled into volume 0's grid; volume 1 goes through
    // the tag-based transform when one is valid.
    std::vector<const Volume*> volumes;
    std::vector<VolumeRenderParams> params;
    for (int vi = 0; vi < numVols; ++vi) {
        volumes.push_back(&state_.volumes_[vi]);
        params.push_back(renderParamsOf(state_.viewStates_[vi]));
    }

    RenderedSlice slice = renderOverlaySlice(
        volumes, params, viewIndex,
        sliceAlong(state_.viewStates_[0].sliceIndices, viewIndex),
        &state_.transformResult_);
    uploadTexture(state_.overlay_.textures[viewIndex], slice);
}

void ViewManager::updateAllOverlayTextures() {
    for (int v = 0; v < 3; ++v)
        updateOverlayTexture(v);
}

void ViewManager::syncCursors() {
    if (!state_.syncCursors_ || state_.volumes_.size() < 2)
        return;
    // Overlay pan/zoom clears the source (-1): nothing to sync from.
    if (state_.lastSyncSource_ < 0 || state_.lastSyncSource_ >= state_.volumeCount())
        return;

    const Volume& refVol = state_.volumes_[state_.lastSyncSource_];
    const VolumeViewState& refState = state_.viewStates_[state_.lastSyncSource_];

    glm::dvec3 worldPos;
    refVol.transformVoxelToWorld(refState.sliceIndices, worldPos);

    for (int i = 0; i < state_.volumeCount(); ++i) {
        if (i == state_.lastSyncSource_)
            continue;

        Volume& otherVol = state_.volumes_[i];
        VolumeViewState& otherState = state_.viewStates_[i];

        glm::ivec3 newVoxel;
        otherVol.transformWorldToVoxel(worldPos, newVoxel);

        otherState.sliceIndices.x = std::clamp(newVoxel.x, 0, otherVol.dimensions.x - 1);
        otherState.sliceIndices.y = std::clamp(newVoxel.y, 0, otherVol.dimensions.y - 1);
        otherState.sliceIndices.z = std::clamp(newVoxel.z, 0, otherVol.dimensions.z - 1);
    }

    for (int i = 0; i < state_.volumeCount(); ++i) {
        for (int v = 0; v < 3; ++v)
            updateSliceTexture(i, v);
    }
    updateAllOverlayTextures();
}

void ViewManager::setOverlayCursor(const glm::ivec3& refIndices) {
    if (state_.viewStates_.empty() || state_.volumes_.empty())
        return;
    const glm::ivec3& dims = state_.volumes_[0].dimensions;
    glm::ivec3& cursor = state_.viewStates_[0].sliceIndices;
    cursor.x = std::clamp(refIndices.x, 0, std::max(dims.x - 1, 0));
    cursor.y = std::clamp(refIndices.y, 0, std::max(dims.y - 1, 0));
    cursor.z = std::clamp(refIndices.z, 0, std::max(dims.z - 1, 0));

    // Other volumes may have a different grid: they follow the same world
    // point via syncCursors() (end of frame), and only when sync is on.
    if (state_.syncCursors_) {
        state_.lastSyncSource_ = 0;
        state_.cursorSyncDirty_ = true;
    }
}

void ViewManager::syncZoom(int sourceVolume, int viewIndex) {
    if (!state_.syncZoom_ || state_.volumes_.size() < 2)
        return;

    double sourceZoom;
    bool fromOverlay = (sourceVolume < 0);

    if (fromOverlay) {
        sourceZoom = state_.overlay_.zoom[viewIndex];
    } else {
        sourceZoom = state_.viewStates_[sourceVolume].zoom[viewIndex];
    }

    for (int i = 0; i < state_.volumeCount(); ++i) {
        if (!fromOverlay && i == sourceVolume)
            continue;
        state_.viewStates_[i].zoom[viewIndex] = sourceZoom;
    }

    if (!fromOverlay) {
        state_.overlay_.zoom[viewIndex] = sourceZoom;
    }
}

void ViewManager::syncPan(int sourceVolume, int viewIndex) {
    if (!state_.syncPan_ || state_.volumes_.size() < 2)
        return;

    double sourcePanU, sourcePanV;
    bool fromOverlay = (sourceVolume < 0);

    if (fromOverlay) {
        sourcePanU = state_.overlay_.panU[viewIndex];
        sourcePanV = state_.overlay_.panV[viewIndex];
    } else {
        sourcePanU = state_.viewStates_[sourceVolume].panU[viewIndex];
        sourcePanV = state_.viewStates_[sourceVolume].panV[viewIndex];
    }

    for (int i = 0; i < state_.volumeCount(); ++i) {
        if (!fromOverlay && i == sourceVolume)
            continue;
        state_.viewStates_[i].panU[viewIndex] = sourcePanU;
        state_.viewStates_[i].panV[viewIndex] = sourcePanV;
    }

    if (!fromOverlay) {
        state_.overlay_.panU[viewIndex] = sourcePanU;
        state_.overlay_.panV[viewIndex] = sourcePanV;
    }
}

void ViewManager::resetViews() {
    for (int vi = 0; vi < state_.volumeCount(); ++vi) {
        const Volume& vol = state_.volumes_[vi];
        if (vol.data.empty())
            continue;

        VolumeViewState& state = state_.viewStates_[vi];

        state.sliceIndices.x = vol.dimensions.x / 2;
        state.sliceIndices.y = vol.dimensions.y / 2;
        state.sliceIndices.z = vol.dimensions.z / 2;

        state.valueRange[0] = vol.min_value;
        state.valueRange[1] = vol.max_value;

        for (int v = 0; v < 3; ++v) {
            state.zoom[v] = 1.0f;
            state.panU[v] = 0.5f;
            state.panV[v] = 0.5f;
        }

        updateSliceTexture(vi, 0);
        updateSliceTexture(vi, 1);
        updateSliceTexture(vi, 2);
    }
}

void ViewManager::initializeAllTextures() {
    for (int vi = 0; vi < state_.volumeCount(); ++vi) {
        if (state_.volumes_[vi].data.empty())
            continue;
        updateSliceTexture(vi, 0);
        updateSliceTexture(vi, 1);
        updateSliceTexture(vi, 2);
    }

    if (state_.hasOverlay())
        updateAllOverlayTextures();
}

void ViewManager::destroyAllTextures() {
    for (auto& vs : state_.viewStates_) {
        for (int i = 0; i < 3; ++i)
            vs.sliceTextures[i].reset();
    }

    for (int i = 0; i < 3; ++i)
        state_.overlay_.textures[i].reset();
}

void ViewManager::sliceIndicesToWorld(const Volume& vol, const int indices[3], double world[3]) {
    glm::dvec4 voxel(indices[0], indices[1], indices[2], 1.0);
    glm::dvec4 worldH = vol.voxelToWorld * voxel;

    world[0] = worldH.x;
    world[1] = worldH.y;
    world[2] = worldH.z;
}

void ViewManager::worldToSliceIndices(const Volume& vol, const double world[3], int indices[3]) {
    glm::dvec4 worldH(world[0], world[1], world[2], 1.0);
    glm::dvec4 voxelH = vol.worldToVoxel * worldH;

    indices[0] = static_cast<int>(std::round(voxelH.x));
    indices[1] = static_cast<int>(std::round(voxelH.y));
    indices[2] = static_cast<int>(std::round(voxelH.z));

    indices[0] = std::clamp(indices[0], 0, vol.dimensions.x - 1);
    indices[1] = std::clamp(indices[1], 0, vol.dimensions.y - 1);
    indices[2] = std::clamp(indices[2], 0, vol.dimensions.z - 1);
}
