#include "ViewManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ColourMap.h"
#include "VulkanHelpers.h"

ViewManager::ViewManager(AppState& state) : state_(state) {}

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
    const ColourLut& lut = colourMapLut(state.colourMap);

    int w, h;
    std::vector<uint32_t> pixels;

    int dimX = vol.dimensions.x;
    int dimY = vol.dimensions.y;
    int dimZ = vol.dimensions.z;

    float rangeMin = state.valueRange[0];
    float rangeMax = state.valueRange[1];
    float rangeSpan = rangeMax - rangeMin;
    if (rangeSpan < 1e-12f)
        rangeSpan = 1e-12f;

    auto voxelToColour = [&](float val) -> uint32_t {
        if (val < rangeMin) {
            int mode = state.underColourMode;
            if (mode == kClampTransparent)
                return 0x00000000;
            ColourMapType mapToUse = state.colourMap;
            if (mode >= 0 && mode < colourMapCount())
                mapToUse = static_cast<ColourMapType>(mode);
            const ColourLut& lut = colourMapLut(mapToUse);
            return lut.table[0];
        }
        if (val > rangeMax) {
            int mode = state.overColourMode;
            if (mode == kClampTransparent)
                return 0x00000000;
            ColourMapType mapToUse = state.colourMap;
            if (mode >= 0 && mode < colourMapCount())
                mapToUse = static_cast<ColourMapType>(mode);
            const ColourLut& lut = colourMapLut(mapToUse);
            return lut.table[255];
        }
        float norm = (val - rangeMin) / rangeSpan;
        int idx = static_cast<int>(norm * 255.0f + 0.5f);
        if (idx > 255)
            idx = 255;
        return lut.table[idx];
    };

    if (viewIndex == 0) {
        w = dimX;
        h = dimY;
        int z = state.sliceIndices.z;
        if (z >= dimZ)
            z = dimZ - 1;

        pixels.resize(w * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                pixels[(h - 1 - y) * w + x] = voxelToColour(vol.get(x, y, z));
            }
        }
    } else if (viewIndex == 1) {
        w = dimY;
        h = dimZ;
        int x = state.sliceIndices.x;
        if (x >= dimX)
            x = dimX - 1;

        pixels.resize(w * h);
        for (int z = 0; z < h; ++z) {
            for (int y = 0; y < w; ++y) {
                pixels[(h - 1 - z) * w + y] = voxelToColour(vol.get(x, y, z));
            }
        }
    } else {
        w = dimX;
        h = dimZ;
        int y = state.sliceIndices.y;
        if (y >= dimY)
            y = dimY - 1;

        pixels.resize(w * h);
        for (int z = 0; z < h; ++z) {
            for (int x = 0; x < w; ++x) {
                pixels[(h - 1 - z) * w + x] = voxelToColour(vol.get(x, y, z));
            }
        }
    }

    std::unique_ptr<VulkanTexture>& tex = state.sliceTextures[viewIndex];
    if (!tex) {
        tex = VulkanHelpers::CreateTexture(w, h, pixels.data());
    } else {
        if (tex->width != w || tex->height != h) {
            VulkanHelpers::DestroyTexture(tex.get());
            tex = VulkanHelpers::CreateTexture(w, h, pixels.data());
        } else {
            VulkanHelpers::UpdateTexture(tex.get(), pixels.data());
        }
    }
}

void ViewManager::updateOverlayTexture(int viewIndex) {
    int numVols = state_.volumeCount();
    if (numVols < 2)
        return;

    const Volume& ref = state_.volumes_[0];
    const VolumeViewState& refState = state_.viewStates_[0];
    if (ref.data.empty())
        return;

    int w, h;
    if (viewIndex == 0) {
        w = ref.dimensions.x;
        h = ref.dimensions.y;
    } else if (viewIndex == 1) {
        w = ref.dimensions.y;
        h = ref.dimensions.z;
    } else {
        w = ref.dimensions.x;
        h = ref.dimensions.z;
    }

    int sliceIdx;
    if (viewIndex == 0)
        sliceIdx = refState.sliceIndices.z;
    else if (viewIndex == 1)
        sliceIdx = refState.sliceIndices.x;
    else
        sliceIdx = refState.sliceIndices.y;

    std::vector<uint32_t> pixels(w * h);

    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
            int refX, refY, refZ;
            if (viewIndex == 0) {
                refX = px;
                refY = py;
                refZ = std::clamp(sliceIdx, 0, ref.dimensions.z - 1);
            } else if (viewIndex == 1) {
                refX = std::clamp(sliceIdx, 0, ref.dimensions.x - 1);
                refY = px;
                refZ = py;
            } else {
                refX = px;
                refY = std::clamp(sliceIdx, 0, ref.dimensions.y - 1);
                refZ = py;
            }

            glm::dvec3 worldPos;
            ref.transformVoxelToWorld(glm::ivec3(refX, refY, refZ), worldPos);

            float accR = 0.0f, accG = 0.0f, accB = 0.0f;
            float totalWeight = 0.0f;

            for (int vi = 0; vi < numVols; ++vi) {
                const Volume& vol = state_.volumes_[vi];
                const VolumeViewState& st = state_.viewStates_[vi];
                if (vol.data.empty())
                    continue;
                if (st.overlayAlpha <= 0.0f)
                    continue;

                glm::ivec3 targetVoxel;
                if (!vol.transformWorldToVoxel(worldPos, targetVoxel))
                    continue;

                float raw = vol.get(targetVoxel.x, targetVoxel.y, targetVoxel.z);
                float rangeMin = st.valueRange[0];
                float rangeMax = st.valueRange[1];
                float rangeSpan = rangeMax - rangeMin;
                if (rangeSpan < 1e-12f)
                    rangeSpan = 1e-12f;

                uint32_t packed;
                if (raw < rangeMin) {
                    int mode = st.underColourMode;
                    if (mode == kClampTransparent) {
                        packed = 0x00000000;
                    } else {
                        ColourMapType mapToUse = st.colourMap;
                        if (mode >= 0 && mode < colourMapCount())
                            mapToUse = static_cast<ColourMapType>(mode);
                        const ColourLut& lut = colourMapLut(mapToUse);
                        packed = lut.table[0];
                    }
                } else if (raw > rangeMax) {
                    int mode = st.overColourMode;
                    if (mode == kClampTransparent) {
                        packed = 0x00000000;
                    } else {
                        ColourMapType mapToUse = st.colourMap;
                        if (mode >= 0 && mode < colourMapCount())
                            mapToUse = static_cast<ColourMapType>(mode);
                        const ColourLut& lut = colourMapLut(mapToUse);
                        packed = lut.table[255];
                    }
                } else {
                    float norm = (raw - rangeMin) / rangeSpan;
                    norm = std::clamp(norm, 0.0f, 1.0f);
                    int lutIdx = std::min(static_cast<int>(norm * 255.0f + 0.5f), 255);
                    const ColourLut& lut = colourMapLut(st.colourMap);
                    packed = lut.table[lutIdx];
                }

                if ((packed >> 24) == 0)
                    continue;

                float srcR = static_cast<float>((packed >> 0) & 0xFF) / 255.0f;
                float srcG = static_cast<float>((packed >> 8) & 0xFF) / 255.0f;
                float srcB = static_cast<float>((packed >> 16) & 0xFF) / 255.0f;
                float w_alpha = st.overlayAlpha;

                accR += srcR * w_alpha;
                accG += srcG * w_alpha;
                accB += srcB * w_alpha;
                totalWeight += w_alpha;
            }

            if (totalWeight > 0.0f) {
                accR /= totalWeight;
                accG /= totalWeight;
                accB /= totalWeight;
            }

            auto toByte = [](float v) -> uint8_t {
                return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
            };

            uint32_t result = static_cast<uint32_t>(toByte(accR))
                            | (static_cast<uint32_t>(toByte(accG)) << 8)
                            | (static_cast<uint32_t>(toByte(accB)) << 16)
                            | (0xFFu << 24);

            pixels[(h - 1 - py) * w + px] = result;
        }
    }

    std::unique_ptr<VulkanTexture>& tex = state_.overlay_.textures[viewIndex];
    if (!tex) {
        tex = VulkanHelpers::CreateTexture(w, h, pixels.data());
    } else {
        if (tex->width != w || tex->height != h) {
            VulkanHelpers::DestroyTexture(tex.get());
            tex = VulkanHelpers::CreateTexture(w, h, pixels.data());
        } else {
            VulkanHelpers::UpdateTexture(tex.get(), pixels.data());
        }
    }
}

void ViewManager::updateAllOverlayTextures() {
    for (int v = 0; v < 3; ++v)
        updateOverlayTexture(v);
}

void ViewManager::syncCursors() {
    if (!state_.syncCursors_ || state_.volumes_.size() < 2)
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
