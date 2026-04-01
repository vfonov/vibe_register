#include "SliceRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// ---------------------------------------------------------------------------
// renderSlice — single-volume 2D slice (port of ViewManager::updateSliceTexture
//               CPU portion, lines 15-183)
// ---------------------------------------------------------------------------

RenderedSlice renderSlice(
    const Volume& vol,
    const VolumeRenderParams& params,
    int viewIndex,
    int sliceIndex)
{
    RenderedSlice result;

    if (vol.data.empty())
        return result;

    // Hoist LUT pointer and colour count
    // Use inverted LUT if invert flag is enabled
    const ColourLut& baseLut = colourMapLut(params.colourMap);
    ColourLut invertedLut;
    const uint32_t* mainLut;
    
    if (params.invertColourMap)
    {
        invertedLut = invertColourLut(baseLut);
        mainLut = invertedLut.table.data();
    }
    else
    {
        mainLut = baseLut.table.data();
    }
    
    int numMaps = colourMapCount();

    int dimX = vol.dimensions.x;
    int dimY = vol.dimensions.y;
    int dimZ = vol.dimensions.z;

    float rangeMin = static_cast<float>(params.valueMin);
    float rangeMax = static_cast<float>(params.valueMax);

    // Log-transform the range if log transform is enabled
    float logRangeMin = rangeMin;
    float logRangeMax = rangeMax;
    
    if (params.useLogTransform)
    {
        // Prevent crash when rangeMin <= 0.0 (log10 undefined for non-positive values)
        // Set lower threshold to -10 (corresponds to log10(1e-10))
        float logLowerThreshold = -10.0f;
        if (rangeMin <= 0.0f)
            logRangeMin = logLowerThreshold;
        else
            logRangeMin = std::log10(rangeMin);
        
        if (rangeMax <= 0.0f)
            logRangeMax = logLowerThreshold;
        else
            logRangeMax = std::log10(rangeMax);
    }

    float rangeSpan = logRangeMax - logRangeMin;
    if (rangeSpan < 1e-12f)
        rangeSpan = 1e-12f;
    float invSpan = 1.0f / rangeSpan;

    // Pre-resolve under/over LUT pointers
    int underMode = params.underColourMode;
    bool underTransparent = (underMode == kSliceClampTransparent);
    uint32_t underColour = 0x00000000;
    if (!underTransparent)
    {
        if (underMode == kSliceClampBlack)
            underColour = 0xFF000000;
        else if (underMode == kSliceClampRed)
            underColour = 0xFF0000FF;
        else if (underMode == kSliceClampGreen)
            underColour = 0xFF00FF00;
        else if (underMode == kSliceClampBlue)
            underColour = 0xFFFF0000;
        else if (underMode == kSliceClampYellow)
            underColour = 0xFF00FFFF;
        else if (underMode == kSliceClampWhite)
            underColour = 0xFFFFFFFF;
        else
        {
            ColourMapType underMap = params.colourMap;
            if (underMode >= 0 && underMode < numMaps)
                underMap = static_cast<ColourMapType>(underMode);
            const ColourLut& underLut = colourMapLut(underMap);
            underColour = params.invertColourMap ? underLut.table[kLutSize - 1] : underLut.table[0];
        }
    }

    int overMode = params.overColourMode;
    bool overTransparent = (overMode == kSliceClampTransparent);
    uint32_t overColour = 0x00000000;
    if (!overTransparent)
    {
        if (overMode == kSliceClampBlack)
            overColour = 0xFF000000;
        else if (overMode == kSliceClampRed)
            overColour = 0xFF0000FF;
        else if (overMode == kSliceClampGreen)
            overColour = 0xFF00FF00;
        else if (overMode == kSliceClampBlue)
            overColour = 0xFFFF0000;
        else if (overMode == kSliceClampYellow)
            overColour = 0xFF00FFFF;
        else if (overMode == kSliceClampWhite)
            overColour = 0xFFFFFFFF;
        else
        {
            ColourMapType overMap = params.colourMap;
            if (overMode >= 0 && overMode < numMaps)
                overMap = static_cast<ColourMapType>(overMode);
            const ColourLut& overLut = colourMapLut(overMap);
            overColour = params.invertColourMap ? overLut.table[0] : overLut.table[255];
        }
    }

    // For label volumes: build label-to-index mapping if a non-default colour
    // map is selected, so labels are rendered via the colour map LUT instead
    // of per-label RGBA.
    bool useColourMapForLabel = vol.isLabelVolume();
    std::unordered_map<int, int> labelToIndex;
    size_t labelCount = 0;
    if (useColourMapForLabel)
    {
        std::vector<int> uniqueLabels = vol.getUniqueLabelIds();
        for (size_t i = 0; i < uniqueLabels.size(); ++i)
            labelToIndex[uniqueLabels[i]] = static_cast<int>(i);
        labelCount = uniqueLabels.size();
    }

    // Lambda: map a raw voxel value to a packed 0xAABBGGRR colour.
    auto voxelToColour = [&](float val) -> uint32_t {
        float displayVal = val;

        // Log transform (applied before colour mapping)
        if (params.useLogTransform)
        {
            // Values <= 0 use under-colour setting
            if (val <= 0.0f)
            {
                if (underTransparent)
                    return 0x00000000;
                return underColour;
            }
            displayVal = std::log10(val);
        }

        // Label volumes
        if (vol.isLabelVolume())
        {
            int labelId = static_cast<int>(displayVal + 0.5f);
            if (labelId == 0)
                return 0x00000000;  // transparent background

            if (useColourMapForLabel)
            {
                auto it = labelToIndex.find(labelId);
                if (it != labelToIndex.end() && labelCount > 0)
                {
                    int idx = (it->second + 1) * 255 / static_cast<int>(labelCount);
                    if (idx < 0)   return underColour;
                    if (idx > 255) return overColour;
                    return mainLut[idx];
                }
                return 0x00000000;  // unknown label
            }

            // Default: use per-label LUT
            const auto& labelLUT = vol.getLabelLUT();
            auto it = labelLUT.find(labelId);
            if (it != labelLUT.end())
            {
                const LabelInfo& info = it->second;
                if (!info.visible)
                    return 0x00000000;
                return static_cast<uint32_t>(info.r) |
                       (static_cast<uint32_t>(info.g) << 8) |
                       (static_cast<uint32_t>(info.b) << 16) |
                       (static_cast<uint32_t>(info.a) << 24);
            }

            // Label not in LUT: deterministic grayscale
            int gray = (labelId * 17) % 256;
            return static_cast<uint32_t>(gray) |
                   (static_cast<uint32_t>(gray) << 8) |
                   (static_cast<uint32_t>(gray) << 16) |
                   0xFF000000;
        }

        // Regular volume: colour map with under/over clamping
        if (displayVal < logRangeMin)
            return underTransparent ? 0x00000000 : underColour;
        if (displayVal > logRangeMax)
            return overTransparent ? 0x00000000 : overColour;
        int idx = static_cast<int>((displayVal - logRangeMin) * invSpan * 255.0f + 0.5f);
        if (idx > 255)
            idx = 255;
        return mainLut[idx];
    };

    const float* vdata = vol.data.data();
    int w, h;

    if (viewIndex == 0)
    {
        // Axial (Z): px=X, py=Y
        w = dimX;
        h = dimY;
        int z = std::clamp(sliceIndex, 0, dimZ - 1);

        result.pixels.resize(w * h);
        int zOff = z * dimY * dimX;
        for (int y = 0; y < h; ++y)
        {
            int rowOff = zOff + y * dimX;
            int dstOff = (h - 1 - y) * w;
            for (int x = 0; x < w; ++x)
                result.pixels[dstOff + x] = voxelToColour(vdata[rowOff + x]);
        }
    }
    else if (viewIndex == 1)
    {
        // Sagittal (X): px=Y, py=Z
        w = dimY;
        h = dimZ;
        int x = std::clamp(sliceIndex, 0, dimX - 1);

        result.pixels.resize(w * h);
        for (int z = 0; z < h; ++z)
        {
            int zOff = z * dimY * dimX + x;
            int dstOff = (h - 1 - z) * w;
            for (int y = 0; y < w; ++y)
                result.pixels[dstOff + y] = voxelToColour(vdata[zOff + y * dimX]);
        }
    }
    else
    {
        // Coronal (Y): px=X, py=Z
        w = dimX;
        h = dimZ;
        int y = std::clamp(sliceIndex, 0, dimY - 1);

        result.pixels.resize(w * h);
        int yOff = y * dimX;
        for (int z = 0; z < h; ++z)
        {
            int zOff = z * dimY * dimX + yOff;
            int dstOff = (h - 1 - z) * w;
            for (int x = 0; x < w; ++x)
                result.pixels[dstOff + x] = voxelToColour(vdata[zOff + x]);
        }
    }

    result.width = w;
    result.height = h;
    return result;
}

// ---------------------------------------------------------------------------
// renderOverlaySlice — multi-volume composite (port of
//                      ViewManager::updateOverlayTexture CPU portion,
//                      lines 198-555)
// ---------------------------------------------------------------------------

RenderedSlice renderOverlaySlice(
    const std::vector<const Volume*>& volumes,
    const std::vector<VolumeRenderParams>& params,
    int viewIndex,
    int sliceIndex,
    const TransformResult* transform)
{
    RenderedSlice result;

    int numVols = static_cast<int>(volumes.size());
    if (numVols < 2)
        return result;

    const Volume& ref = *volumes[0];
    if (ref.data.empty())
        return result;

    int w, h;
    if (viewIndex == 0)
    {
        w = ref.dimensions.x;
        h = ref.dimensions.y;
    }
    else if (viewIndex == 1)
    {
        w = ref.dimensions.y;
        h = ref.dimensions.z;
    }
    else
    {
        w = ref.dimensions.x;
        h = ref.dimensions.z;
    }

    // --- Per-volume precomputed data ---
    struct PerVolInfo
    {
        glm::dmat4 worldToVox;       // vol.worldToVoxel, applied per-pixel
        const float* vdata;
        glm::ivec3 dims;
        int dimXY;
        float rangeMin, rangeMax, invSpan;
        float logRangeMin, logRangeMax;  // Log-transformed range values
        const uint32_t* mainLut;
        std::array<uint32_t, 256> ownedLut{};  // storage when invertColourMap (prevents dangling ptr)
        uint32_t underColour, overColour;
        bool underTransparent, overTransparent;
        float alpha;
        bool isRef    = false;  // vi==0: sample at (rx,ry,rz) directly
        bool useTPS   = false;  // vi==1 with TPS transform
        int  volIndex = 0;      // original vi, for transform dispatch
        bool isLabelVolume = false;
        const std::unordered_map<int, LabelInfo>* labelLUT = nullptr;
        bool useColourMapForLabel = false;
        std::unordered_map<int, int> labelToIndex;
        size_t labelCacheSize = 0;
        bool useLogTransform = false;
    };

    int numMaps = colourMapCount();
    std::vector<PerVolInfo> infos;
    infos.reserve(numVols);

    // Determine transform availability
    bool hasTransform = (transform != nullptr && transform->valid && numVols >= 2);
    bool hasLinearTransform = hasTransform && (transform->type != TransformType::TPS);
    bool hasTPSTransform = hasTransform && (transform->type == TransformType::TPS);

    glm::dmat4 invLinear{1.0};
    if (hasLinearTransform)
        invLinear = glm::inverse(transform->linearMatrix);

    for (int vi = 0; vi < numVols; ++vi)
    {
        const Volume& vol = *volumes[vi];
        const VolumeRenderParams& p = params[vi];
        if (vol.data.empty() || p.overlayAlpha <= 0.0f)
            continue;

        PerVolInfo info;

        info.worldToVox = vol.worldToVoxel;
        info.isRef      = (vi == 0);
        info.useTPS     = (vi == 1 && hasTPSTransform);
        info.volIndex   = vi;

        info.vdata = vol.data.data();
        info.dims = vol.dimensions;
        info.dimXY = vol.dimensions.x * vol.dimensions.y;
        info.rangeMin = static_cast<float>(p.valueMin);
        info.rangeMax = static_cast<float>(p.valueMax);

        // Log-transform the range if log transform is enabled
        float logRangeMin = info.rangeMin;
        float logRangeMax = info.rangeMax;
        if (p.useLogTransform)
        {
            float logLowerThreshold = -10.0f;
            logRangeMin = (info.rangeMin <= 0.0f) ? logLowerThreshold : std::log10(info.rangeMin);
            logRangeMax = (info.rangeMax <= 0.0f) ? logLowerThreshold : std::log10(info.rangeMax);
        }

        float span = logRangeMax - logRangeMin;
        if (span < 1e-12f)
            span = 1e-12f;
        info.invSpan = 1.0f / span;
        info.logRangeMin = logRangeMin;
        info.logRangeMax = logRangeMax;
        // Use inverted LUT if invert flag is enabled
        const ColourLut& baseLut = colourMapLut(p.colourMap);
        if (p.invertColourMap)
        {
            ColourLut invertedLut = invertColourLut(baseLut);
            info.ownedLut = invertedLut.table;  // own the storage
            info.mainLut  = info.ownedLut.data();
        }
        else
        {
            info.mainLut = baseLut.table.data();
        }
        info.alpha = p.overlayAlpha;

        // Pre-resolve under/over colours
        int underMode = p.underColourMode;
        info.underTransparent = (underMode == kSliceClampTransparent);
        info.underColour = 0x00000000;
        if (!info.underTransparent)
        {
            if (underMode == kSliceClampBlack)
                info.underColour = 0xFF000000;
            else if (underMode == kSliceClampRed)
                info.underColour = 0xFF0000FF;
            else if (underMode == kSliceClampGreen)
                info.underColour = 0xFF00FF00;
            else if (underMode == kSliceClampBlue)
                info.underColour = 0xFFFF0000;
            else if (underMode == kSliceClampYellow)
                info.underColour = 0xFF00FFFF;
            else if (underMode == kSliceClampWhite)
                info.underColour = 0xFFFFFFFF;
            else
            {
                ColourMapType underMap = p.colourMap;
                if (underMode >= 0 && underMode < numMaps)
                    underMap = static_cast<ColourMapType>(underMode);
                const ColourLut& underLut = colourMapLut(underMap);
                info.underColour = p.invertColourMap ? underLut.table[kLutSize - 1] : underLut.table[0];
            }
        }

        int overMode = p.overColourMode;
        info.overTransparent = (overMode == kSliceClampTransparent);
        info.overColour = 0x00000000;
        if (!info.overTransparent)
        {
            if (overMode == kSliceClampBlack)
                info.overColour = 0xFF000000;
            else if (overMode == kSliceClampRed)
                info.overColour = 0xFF0000FF;
            else if (overMode == kSliceClampGreen)
                info.overColour = 0xFF00FF00;
            else if (overMode == kSliceClampBlue)
                info.overColour = 0xFFFF0000;
            else if (overMode == kSliceClampYellow)
                info.overColour = 0xFF00FFFF;
            else if (overMode == kSliceClampWhite)
                info.overColour = 0xFFFFFFFF;
            else
            {
                ColourMapType overMap = p.colourMap;
                if (overMode >= 0 && overMode < numMaps)
                    overMap = static_cast<ColourMapType>(overMode);
                const ColourLut& overLut = colourMapLut(overMap);
                info.overColour = p.invertColourMap ? overLut.table[0] : overLut.table[255];
            }
        }

        // Label volume support
        info.isLabelVolume = vol.isLabelVolume();
        if (info.isLabelVolume)
        {
            info.labelLUT = &vol.getLabelLUT();
            {
                info.useColourMapForLabel = true;
                std::vector<int> uniqueLabels = vol.getUniqueLabelIds();
                for (size_t i = 0; i < uniqueLabels.size(); ++i)
                    info.labelToIndex[uniqueLabels[i]] = static_cast<int>(i);
                info.labelCacheSize = uniqueLabels.size();
            }
        }

        // Log transform setting
        info.useLogTransform = p.useLogTransform;

        infos.push_back(std::move(info));
    }

    result.pixels.resize(w * h);

    const glm::dmat4& refV2W = ref.voxelToWorld;

    // Pixel loop — for each output pixel, compute ref voxel (rx,ry,rz) directly,
    // convert to world once, then map to each target volume's voxel space.
    for (int py = 0; py < h; ++py)
    {
        int dstRowOff = (h - 1 - py) * w;

        for (int px = 0; px < w; ++px)
        {
            float accR = 0.0f, accG = 0.0f, accB = 0.0f;
            float totalWeight = 0.0f;

            // Reference voxel coordinates (no clamping — out-of-bounds → background)
            int rx, ry, rz;
            if      (viewIndex == 0) { rx = px; ry = py; rz = sliceIndex; }
            else if (viewIndex == 1) { rx = sliceIndex; ry = px; rz = py; }
            else                     { rx = px; ry = sliceIndex; rz = py; }

            // World position of this ref voxel — computed once, reused for all volumes
            glm::dvec4 world = refV2W * glm::dvec4(
                static_cast<double>(rx), static_cast<double>(ry),
                static_cast<double>(rz), 1.0);

            for (size_t vi = 0; vi < infos.size(); ++vi)
            {
                const auto& info = infos[vi];

                // ── Compute fractional target voxel ──────────────────────
                glm::dvec3 tv;
                if (info.isRef)
                {
                    tv = glm::dvec3(rx, ry, rz);
                }
                else if (info.volIndex == 1 && info.useTPS)
                {
                    glm::dvec3 vw = transform->inverseTransformPoint(glm::dvec3(world));
                    tv = glm::dvec3(info.worldToVox * glm::dvec4(vw, 1.0));
                }
                else if (info.volIndex == 1 && hasLinearTransform)
                {
                    tv = glm::dvec3(info.worldToVox * (invLinear * world));
                }
                else
                {
                    tv = glm::dvec3(info.worldToVox * world);
                }

                // ── Out-of-bounds → skip (no contribution) ───────────────
                // Use half-voxel extended range to match nearest-neighbour extent.
                if (tv.x < -0.5 || tv.x >= info.dims.x - 0.5 ||
                    tv.y < -0.5 || tv.y >= info.dims.y - 0.5 ||
                    tv.z < -0.5 || tv.z >= info.dims.z - 0.5)
                    continue;

                // ── Sample voxel value (nearest-neighbour) ───────────────
                float raw;
                {
                    int tx = std::clamp(static_cast<int>(std::round(tv.x)), 0, info.dims.x - 1);
                    int ty = std::clamp(static_cast<int>(std::round(tv.y)), 0, info.dims.y - 1);
                    int tz = std::clamp(static_cast<int>(std::round(tv.z)), 0, info.dims.z - 1);
                    raw = info.vdata[tz * info.dimXY + ty * info.dims.x + tx];
                }

                uint32_t packed;
                {
                // Apply log transform if enabled
                float displayVal = raw;
                bool logSkipPixel = false;
                uint32_t logSkipColour = 0;

                if (info.useLogTransform)
                {
                    if (displayVal <= 0.0f)
                    {
                        // Non-positive values use under-colour setting
                        if (info.underTransparent)
                            continue;
                        logSkipColour = info.underColour;
                        logSkipPixel = true;
                    }
                    else
                    {
                        displayVal = std::log10(displayVal);
                    }
                }

                if (logSkipPixel)
                {
                    packed = logSkipColour;
                }
                else if (info.isLabelVolume && !info.useLogTransform)
                {
                    int labelId = static_cast<int>(displayVal + 0.5f);
                    if (labelId == 0)
                        continue;

                    if (info.useColourMapForLabel)
                    {
                        auto it = info.labelToIndex.find(labelId);
                        if (it != info.labelToIndex.end() && info.labelCacheSize > 0)
                        {
                            int idx = (it->second + 1) * 255 / static_cast<int>(info.labelCacheSize);
                            if (idx < 0)        packed = info.underColour;
                            else if (idx > 255) packed = info.overColour;
                            else                packed = info.mainLut[idx];
                        }
                        else
                        {
                            continue;  // unknown label
                        }
                    }
                    else if (info.labelLUT)
                    {
                        auto it = info.labelLUT->find(labelId);
                        if (it != info.labelLUT->end())
                        {
                            const LabelInfo& lbl = it->second;
                            if (!lbl.visible)
                                continue;
                            packed = static_cast<uint32_t>(lbl.r) |
                                     (static_cast<uint32_t>(lbl.g) << 8) |
                                     (static_cast<uint32_t>(lbl.b) << 16) |
                                     (static_cast<uint32_t>(lbl.a) << 24);
                            if (lbl.a == 0)
                                continue;
                        }
                        else
                        {
                            int gray = (labelId * 17) % 256;
                            packed = static_cast<uint32_t>(gray) |
                                     (static_cast<uint32_t>(gray) << 8) |
                                     (static_cast<uint32_t>(gray) << 16) |
                                     0xFF000000;
                        }
                    }
                    else
                    {
                        int gray = (labelId * 17) % 256;
                        packed = static_cast<uint32_t>(gray) |
                                 (static_cast<uint32_t>(gray) << 8) |
                                 (static_cast<uint32_t>(gray) << 16) |
                                 0xFF000000;
                    }
                }
                else if (displayVal < info.logRangeMin)
                {
                    if (info.underTransparent)
                        continue;
                    packed = info.underColour;
                }
                else if (displayVal > info.logRangeMax)
                {
                    if (info.overTransparent)
                        continue;
                    packed = info.overColour;
                }
                else
                {
                    int lutIdx = static_cast<int>(
                        (displayVal - info.logRangeMin) * info.invSpan * 255.0f + 0.5f);
                    if (lutIdx > 255)
                        lutIdx = 255;
                    packed = info.mainLut[lutIdx];
                }

                } // end inBounds else

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

            if (totalWeight > 0.0f)
            {
                float inv = 1.0f / totalWeight;
                accR *= inv;
                accG *= inv;
                accB *= inv;
            }

            auto toByte = [](float v) -> uint32_t {
                int c = static_cast<int>(v * 255.0f + 0.5f);
                return static_cast<uint32_t>(c < 0 ? 0 : (c > 255 ? 255 : c));
            };

            result.pixels[dstRowOff + px] = toByte(accR)
                                           | (toByte(accG) << 8)
                                           | (toByte(accB) << 16)
                                           | (0xFFu << 24);
        }
    }

    result.width = w;
    result.height = h;
    return result;
}
