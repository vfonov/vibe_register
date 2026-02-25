#include "ViewManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ColourMap.h"
#include "GraphicsBackend.h"
#include "Transform.h"
#include "Volume.h"

ViewManager::ViewManager(AppState& state, GraphicsBackend& backend)
    : state_(state), backend_(backend) {}

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

    // Hoist LUT pointers and colour count outside the pixel loop
    const uint32_t* mainLut = colourMapLut(state.colourMap).table.data();
    int numMaps = colourMapCount();

    int w, h;

    int dimX = vol.dimensions.x;
    int dimY = vol.dimensions.y;
    int dimZ = vol.dimensions.z;

    float rangeMin = static_cast<float>(state.valueRange[0]);
    float rangeMax = static_cast<float>(state.valueRange[1]);
    float rangeSpan = rangeMax - rangeMin;
    if (rangeSpan < 1e-12f)
        rangeSpan = 1e-12f;
    float invSpan = 1.0f / rangeSpan;

    // Pre-resolve under/over LUT pointers to avoid per-pixel lookups
    int underMode = state.underColourMode;
    uint32_t underColour = 0x00000000;
    bool underTransparent = (underMode == kClampTransparent);
    if (!underTransparent)
    {
        ColourMapType underMap = state.colourMap;
        if (underMode >= 0 && underMode < numMaps)
            underMap = static_cast<ColourMapType>(underMode);
        underColour = colourMapLut(underMap).table[0];
    }

    int overMode = state.overColourMode;
    uint32_t overColour = 0x00000000;
    bool overTransparent = (overMode == kClampTransparent);
    if (!overTransparent)
    {
        ColourMapType overMap = state.colourMap;
        if (overMode >= 0 && overMode < numMaps)
            overMap = static_cast<ColourMapType>(overMode);
        overColour = colourMapLut(overMap).table[255];
    }

    // For label volumes: check if we should use colour map instead of label LUT
    bool useColourMapForLabel = vol.isLabelVolume() && state.colourMap != ColourMapType::GrayScale;
    const std::unordered_map<int, int>* labelToIndexPtr = nullptr;
    size_t labelCount = 0;
    if (useColourMapForLabel) {
        auto cacheIt = labelToIndexCache_.find(volumeIndex);
        if (cacheIt == labelToIndexCache_.end()) {
            std::vector<int> uniqueLabels = vol.getUniqueLabelIds();
            std::unordered_map<int, int> mapping;
            for (size_t i = 0; i < uniqueLabels.size(); ++i) {
                mapping[uniqueLabels[i]] = static_cast<int>(i);
            }
            labelCount = uniqueLabels.size();
            labelToIndexCache_[volumeIndex] = std::move(mapping);
            labelCacheSize_[volumeIndex] = labelCount;
            cacheIt = labelToIndexCache_.find(volumeIndex);
        } else {
            labelCount = labelCacheSize_[volumeIndex];
        }
        labelToIndexPtr = &cacheIt->second;
    }

    auto voxelToColour = [&](float val) -> uint32_t {
        // Handle label volumes: use colour map if selected, otherwise use label LUT
        if (vol.isLabelVolume()) {
            int labelId = static_cast<int>(std::round(val));
            if (labelId == 0) {
                return 0x00000000;  // transparent background
            }
            // Use colour map if a non-default one is selected
            if (useColourMapForLabel && labelToIndexPtr) {
                auto it = labelToIndexPtr->find(labelId);
                if (it != labelToIndexPtr->end()) {
                    int idx = static_cast<int>((static_cast<float>(it->second) / static_cast<float>(labelCount)) * 255.0f);
                    return mainLut[idx];
                }
                return 0x00000000;  // unknown label
            }
            // Default: use label LUT
            const auto& labelLUT = vol.getLabelLUT();
            auto it = labelLUT.find(labelId);
            if (it != labelLUT.end()) {
                const LabelInfo& info = it->second;
                if (!info.visible) {
                    return 0x00000000;  // invisible label
                }
                // Pack RGBA: R in bits 0-7, G in 8-15, B in 16-23, A in 24-31
                return static_cast<uint32_t>(info.r) |
                       (static_cast<uint32_t>(info.g) << 8) |
                       (static_cast<uint32_t>(info.b) << 16) |
                       (static_cast<uint32_t>(info.a) << 24);
            }
            // Label not in LUT: use grayscale based on label ID
            int gray = (labelId * 17) % 256;
            return static_cast<uint32_t>(gray) |
                   (static_cast<uint32_t>(gray) << 8) |
                   (static_cast<uint32_t>(gray) << 16) |
                   0xFF000000;  // fully opaque
        }

        // Regular volume: use colour map
        if (val < rangeMin)
            return underTransparent ? 0x00000000 : underColour;
        if (val > rangeMax)
            return overTransparent ? 0x00000000 : overColour;
        int idx = static_cast<int>((val - rangeMin) * invSpan * 255.0f + 0.5f);
        if (idx > 255)
            idx = 255;
        return mainLut[idx];
    };

    // Direct pointer to volume data for unchecked linear indexing
    const float* vdata = vol.data.data();

    if (viewIndex == 0) {
        w = dimX;
        h = dimY;
        int z = std::clamp(state.sliceIndices.z, 0, dimZ - 1);

        pixelBuf_.resize(w * h);
        int zOff = z * dimY * dimX;
        for (int y = 0; y < h; ++y) {
            int rowOff = zOff + y * dimX;
            int dstOff = (h - 1 - y) * w;
            for (int x = 0; x < w; ++x) {
                pixelBuf_[dstOff + x] = voxelToColour(vdata[rowOff + x]);
            }
        }
    } else if (viewIndex == 1) {
        w = dimY;
        h = dimZ;
        int x = std::clamp(state.sliceIndices.x, 0, dimX - 1);

        pixelBuf_.resize(w * h);
        for (int z = 0; z < h; ++z) {
            int zOff = z * dimY * dimX + x;
            int dstOff = (h - 1 - z) * w;
            for (int y = 0; y < w; ++y) {
                pixelBuf_[dstOff + y] = voxelToColour(vdata[zOff + y * dimX]);
            }
        }
    } else {
        w = dimX;
        h = dimZ;
        int y = std::clamp(state.sliceIndices.y, 0, dimY - 1);

        pixelBuf_.resize(w * h);
        int yOff = y * dimX;
        for (int z = 0; z < h; ++z) {
            int zOff = z * dimY * dimX + yOff;
            int dstOff = (h - 1 - z) * w;
            for (int x = 0; x < w; ++x) {
                pixelBuf_[dstOff + x] = voxelToColour(vdata[zOff + x]);
            }
        }
    }

    std::unique_ptr<Texture>& tex = state.sliceTextures[viewIndex];
    if (!tex) {
        tex = backend_.createTexture(w, h, pixelBuf_.data());
    } else {
        if (tex->width != w || tex->height != h) {
            backend_.destroyTexture(tex.get());
            tex = backend_.createTexture(w, h, pixelBuf_.data());
        } else {
            backend_.updateTexture(tex.get(), pixelBuf_.data());
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

    // --- Per-volume precomputed data ---
    // For each volume we precompute:
    //   combined = vol.worldToVoxel * ref.voxelToWorld  (ref-voxel -> target-voxel)
    //   LUT pointer, range params, under/over colours, overlay alpha
    //
    // When a valid tag-based transform exists for volume 1 (the second volume),
    // the transform is inserted into the chain:
    //   Linear: combined = vol1.worldToVoxel * T^{-1} * ref.voxelToWorld
    //   TPS:    per-pixel world-space inversion (no scanline optimization)
    struct PerVolInfo {
        glm::dmat4 combined;         // ref-voxel -> target-voxel transform
        const float* vdata;          // pointer to volume data
        glm::ivec3 dims;             // target volume dimensions
        int dimXY;                   // dims.x * dims.y
        float rangeMin, rangeMax, invSpan;
        const uint32_t* mainLut;
        uint32_t underColour, overColour;
        bool underTransparent, overTransparent;
        float alpha;
        bool useTPSInverse = false;  // true if TPS per-pixel inversion needed
        glm::dmat4 targetWorldToVox; // for TPS path: target vol worldToVoxel
        bool isLabelVolume = false;  // true if this is a label/segmentation volume
        const std::unordered_map<int, LabelInfo>* labelLUT = nullptr;  // label colour lookup
        bool useColourMapForLabel = false;  // use colour map instead of label LUT
        std::unordered_map<int, int> labelToIndex;  // label ID to colour map index
        size_t labelCacheSize = 0;  // number of unique labels
    };

    int numMaps = colourMapCount();
    std::vector<PerVolInfo> infos;
    infos.reserve(numVols);

    // Check if a valid tag-based transform is available for volume 1
    const TransformResult& xfmResult = state_.transformResult_;
    bool hasTransform = (xfmResult.valid && numVols >= 2);
    bool hasLinearTransform = hasTransform && (xfmResult.type != TransformType::TPS);
    bool hasTPSTransform = hasTransform && (xfmResult.type == TransformType::TPS);

    // Precompute the inverse linear matrix when available
    glm::dmat4 invLinear{1.0};
    if (hasLinearTransform)
        invLinear = glm::inverse(xfmResult.linearMatrix);

    for (int vi = 0; vi < numVols; ++vi) {
        const Volume& vol = state_.volumes_[vi];
        const VolumeViewState& st = state_.viewStates_[vi];
        if (vol.data.empty() || st.overlayAlpha <= 0.0f)
            continue;

        PerVolInfo info;

        if (vi == 1 && hasLinearTransform)
        {
            // Insert inverse linear transform: ref-voxel -> world -> vol1-world -> vol1-voxel
            info.combined = vol.worldToVoxel * invLinear * ref.voxelToWorld;
        }
        else if (vi == 1 && hasTPSTransform)
        {
            // TPS needs per-pixel world-space inversion; combined is not used
            // for the scanline path.  We still set a combined for the scanline
            // precomputation, but flag this volume for per-pixel handling.
            info.combined = vol.worldToVoxel * ref.voxelToWorld;  // unused in pixel loop
            info.useTPSInverse = true;
            info.targetWorldToVox = vol.worldToVoxel;
        }
        else
        {
            info.combined = vol.worldToVoxel * ref.voxelToWorld;
        }

        info.vdata = vol.data.data();
        info.dims = vol.dimensions;
        info.dimXY = vol.dimensions.x * vol.dimensions.y;
        info.rangeMin = static_cast<float>(st.valueRange[0]);
        info.rangeMax = static_cast<float>(st.valueRange[1]);
        float span = info.rangeMax - info.rangeMin;
        if (span < 1e-12f) span = 1e-12f;
        info.invSpan = 1.0f / span;
        info.mainLut = colourMapLut(st.colourMap).table.data();
        info.alpha = st.overlayAlpha;

        // Pre-resolve under/over colours
        int underMode = st.underColourMode;
        info.underTransparent = (underMode == kClampTransparent);
        info.underColour = 0x00000000;
        if (!info.underTransparent) {
            ColourMapType underMap = st.colourMap;
            if (underMode >= 0 && underMode < numMaps)
                underMap = static_cast<ColourMapType>(underMode);
            info.underColour = colourMapLut(underMap).table[0];
        }

        int overMode = st.overColourMode;
        info.overTransparent = (overMode == kClampTransparent);
        info.overColour = 0x00000000;
        if (!info.overTransparent) {
            ColourMapType overMap = st.colourMap;
            if (overMode >= 0 && overMode < numMaps)
                overMap = static_cast<ColourMapType>(overMode);
            info.overColour = colourMapLut(overMap).table[255];
        }

        // Label volume support
        info.isLabelVolume = vol.isLabelVolume();
        if (info.isLabelVolume) {
            info.labelLUT = &vol.getLabelLUT();
            // Check if we should use colour map instead of label LUT
            if (state_.viewStates_[vi].colourMap != ColourMapType::GrayScale) {
                info.useColourMapForLabel = true;
                auto cacheIt = labelToIndexCache_.find(vi);
                if (cacheIt == labelToIndexCache_.end()) {
                    std::vector<int> uniqueLabels = vol.getUniqueLabelIds();
                    std::unordered_map<int, int> mapping;
                    for (size_t i = 0; i < uniqueLabels.size(); ++i) {
                        mapping[uniqueLabels[i]] = static_cast<int>(i);
                    }
                    labelCacheSize_[vi] = uniqueLabels.size();
                    labelToIndexCache_[vi] = std::move(mapping);
                    cacheIt = labelToIndexCache_.find(vi);
                }
                info.labelToIndex = cacheIt->second;
                info.labelCacheSize = labelCacheSize_[vi];
            }
        }

        infos.push_back(info);
    }

    pixelBuf_.resize(w * h);

    // Precompute the ref-voxel base and row/col deltas for the 2D scan.
    // This avoids a full 4x4 matrix multiply per pixel per volume.
    // ref-voxel as doubles: base is the corner of the slice, dPx/dPy are deltas.
    glm::dvec3 refBase, refDpx, refDpy;
    if (viewIndex == 0) {
        // Transverse: px=refX, py=refY, refZ=sliceIdx
        int z = std::clamp(sliceIdx, 0, ref.dimensions.z - 1);
        refBase = glm::dvec3(0.0, 0.0, static_cast<double>(z));
        refDpx = glm::dvec3(1.0, 0.0, 0.0);
        refDpy = glm::dvec3(0.0, 1.0, 0.0);
    } else if (viewIndex == 1) {
        // Sagittal: px=refY, py=refZ, refX=sliceIdx
        int x = std::clamp(sliceIdx, 0, ref.dimensions.x - 1);
        refBase = glm::dvec3(static_cast<double>(x), 0.0, 0.0);
        refDpx = glm::dvec3(0.0, 1.0, 0.0);
        refDpy = glm::dvec3(0.0, 0.0, 1.0);
    } else {
        // Coronal: px=refX, py=refZ, refY=sliceIdx
        int y = std::clamp(sliceIdx, 0, ref.dimensions.y - 1);
        refBase = glm::dvec3(0.0, static_cast<double>(y), 0.0);
        refDpx = glm::dvec3(1.0, 0.0, 0.0);
        refDpy = glm::dvec3(0.0, 0.0, 1.0);
    }

    // For each target volume, precompute the combined scanline parameters:
    //   targetBase  = combined * (refBase, 1)
    //   targetDpx   = combined * (refDpx, 0)  (direction, no translation)
    //   targetDpy   = combined * (refDpy, 0)
    // For TPS volumes these are unused; we compute world coords per-pixel.
    struct ScanInfo {
        glm::dvec3 base;   // target-voxel at (px=0, py=0)
        glm::dvec3 dpx;    // target-voxel delta per px++
        glm::dvec3 dpy;    // target-voxel delta per py++
    };
    std::vector<ScanInfo> scans(infos.size());
    for (size_t i = 0; i < infos.size(); ++i) {
        if (infos[i].useTPSInverse)
            continue;  // scanline not used for TPS volumes
        const auto& M = infos[i].combined;
        glm::dvec4 bH = M * glm::dvec4(refBase, 1.0);
        glm::dvec4 dxH = M * glm::dvec4(refDpx, 0.0);
        glm::dvec4 dyH = M * glm::dvec4(refDpy, 0.0);
        scans[i].base = glm::dvec3(bH);
        scans[i].dpx = glm::dvec3(dxH);
        scans[i].dpy = glm::dvec3(dyH);
    }

    // For TPS per-pixel path: precompute ref-voxel -> world scanline params
    // so we can efficiently compute the world coordinate for each pixel.
    glm::dvec3 worldBase(0.0), worldDpx(0.0), worldDpy(0.0);
    bool anyTPS = false;
    for (const auto& info : infos)
    {
        if (info.useTPSInverse)
        {
            anyTPS = true;
            break;
        }
    }
    if (anyTPS)
    {
        const auto& V2W = ref.voxelToWorld;
        glm::dvec4 bH  = V2W * glm::dvec4(refBase, 1.0);
        glm::dvec4 dxH = V2W * glm::dvec4(refDpx, 0.0);
        glm::dvec4 dyH = V2W * glm::dvec4(refDpy, 0.0);
        worldBase = glm::dvec3(bH);
        worldDpx  = glm::dvec3(dxH);
        worldDpy  = glm::dvec3(dyH);
    }

    for (int py = 0; py < h; ++py) {
        int dstRowOff = (h - 1 - py) * w;

        for (int px = 0; px < w; ++px) {
            float accR = 0.0f, accG = 0.0f, accB = 0.0f;
            float totalWeight = 0.0f;

            for (size_t vi = 0; vi < infos.size(); ++vi) {
                const auto& info = infos[vi];

                glm::dvec3 tv;
                if (info.useTPSInverse)
                {
                    // TPS per-pixel path: ref-voxel -> world -> TPS inverse -> target-world -> target-voxel
                    glm::dvec3 worldPt = worldBase + static_cast<double>(px) * worldDpx
                                                   + static_cast<double>(py) * worldDpy;
                    glm::dvec3 vol1World = xfmResult.inverseTransformPoint(worldPt);
                    glm::dvec4 vox = info.targetWorldToVox * glm::dvec4(vol1World, 1.0);
                    tv = glm::dvec3(vox);
                }
                else
                {
                    // Scanline-optimized path (linear or identity)
                    const auto& sc = scans[vi];
                    tv = sc.base + static_cast<double>(px) * sc.dpx
                                 + static_cast<double>(py) * sc.dpy;
                }

                int tx = static_cast<int>(std::round(tv.x));
                int ty = static_cast<int>(std::round(tv.y));
                int tz = static_cast<int>(std::round(tv.z));

                // Bounds check
                if (tx < 0 || tx >= info.dims.x ||
                    ty < 0 || ty >= info.dims.y ||
                    tz < 0 || tz >= info.dims.z)
                    continue;

                float raw = info.vdata[tz * info.dimXY + ty * info.dims.x + tx];

                uint32_t packed;
                if (info.isLabelVolume) {
                    int labelId = static_cast<int>(std::round(raw));
                    if (labelId == 0) {
                        continue;  // transparent background
                    }
                    // Use colour map if selected
                    if (info.useColourMapForLabel) {
                        auto it = info.labelToIndex.find(labelId);
                        if (it != info.labelToIndex.end() && info.labelCacheSize > 0) {
                            int idx = static_cast<int>((static_cast<float>(it->second) / static_cast<float>(info.labelCacheSize)) * 255.0f);
                            packed = info.mainLut[idx];
                        } else {
                            continue;  // unknown label
                        }
                    } else if (info.labelLUT) {
                        // Use label LUT
                        auto it = info.labelLUT->find(labelId);
                        if (it != info.labelLUT->end()) {
                            const LabelInfo& lbl = it->second;
                            if (!lbl.visible) {
                                continue;  // invisible label
                            }
                            packed = static_cast<uint32_t>(lbl.r) |
                                     (static_cast<uint32_t>(lbl.g) << 8) |
                                     (static_cast<uint32_t>(lbl.b) << 16) |
                                     (static_cast<uint32_t>(lbl.a) << 24);
                            if (lbl.a == 0) continue;
                        } else {
                            // Label not in LUT: use grayscale based on label ID
                            int gray = (labelId * 17) % 256;
                            packed = static_cast<uint32_t>(gray) |
                                     (static_cast<uint32_t>(gray) << 8) |
                                     (static_cast<uint32_t>(gray) << 16) |
                                     0xFF000000;
                        }
                    } else {
                        // No label LUT: use grayscale based on label ID
                        int gray = (labelId * 17) % 256;
                        packed = static_cast<uint32_t>(gray) |
                                 (static_cast<uint32_t>(gray) << 8) |
                                 (static_cast<uint32_t>(gray) << 16) |
                                 0xFF000000;
                    }
                } else if (raw < info.rangeMin) {
                    if (info.underTransparent) continue;
                    packed = info.underColour;
                } else if (raw > info.rangeMax) {
                    if (info.overTransparent) continue;
                    packed = info.overColour;
                } else {
                    int lutIdx = static_cast<int>(
                        (raw - info.rangeMin) * info.invSpan * 255.0f + 0.5f);
                    if (lutIdx > 255) lutIdx = 255;
                    packed = info.mainLut[lutIdx];
                }

                if ((packed >> 24) == 0)
                    continue;

                float srcR = static_cast<float>((packed >> 0) & 0xFF) * (1.0f / 255.0f);
                float srcG = static_cast<float>((packed >> 8) & 0xFF) * (1.0f / 255.0f);
                float srcB = static_cast<float>((packed >> 16) & 0xFF) * (1.0f / 255.0f);

                accR += srcR * info.alpha;
                accG += srcG * info.alpha;
                accB += srcB * info.alpha;
                totalWeight += info.alpha;
            }

            if (totalWeight > 0.0f) {
                float inv = 1.0f / totalWeight;
                accR *= inv;
                accG *= inv;
                accB *= inv;
            }

            auto toByte = [](float v) -> uint32_t {
                int c = static_cast<int>(v * 255.0f + 0.5f);
                return static_cast<uint32_t>(c < 0 ? 0 : (c > 255 ? 255 : c));
            };

            pixelBuf_[dstRowOff + px] = toByte(accR)
                                       | (toByte(accG) << 8)
                                       | (toByte(accB) << 16)
                                       | (0xFFu << 24);
        }
    }

    std::unique_ptr<Texture>& tex = state_.overlay_.textures[viewIndex];
    if (!tex) {
        tex = backend_.createTexture(w, h, pixelBuf_.data());
    } else {
        if (tex->width != w || tex->height != h) {
            backend_.destroyTexture(tex.get());
            tex = backend_.createTexture(w, h, pixelBuf_.data());
        } else {
            backend_.updateTexture(tex.get(), pixelBuf_.data());
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

void ViewManager::invalidateLabelCache(int volumeIndex) {
    labelToIndexCache_.erase(volumeIndex);
    labelCacheSize_.erase(volumeIndex);
}
