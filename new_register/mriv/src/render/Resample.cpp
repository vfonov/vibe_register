#include "render/Resample.hpp"

#include <algorithm>
#include <cmath>

namespace mriv::term
{

ResampleSize computeResampleSize(int w, int h, double aspect, int maxW, int maxH)
{
    double effectiveW = w * aspect;
    double effectiveH = h;

    double scale = std::min(1.0, std::min(maxW / effectiveW, maxH / effectiveH));

    int outW = std::max(1, static_cast<int>(std::lround(effectiveW * scale)));
    int outH = std::max(1, static_cast<int>(std::lround(effectiveH * scale)));

    return ResampleSize{outW, outH};
}

std::vector<uint32_t> resamplePixelsNearest(
    const uint32_t* src, int w, int h, int outW, int outH)
{
    std::vector<uint32_t> out(static_cast<size_t>(outW) * outH);

    for (int dy = 0; dy < outH; ++dy)
    {
        int srcY = std::clamp(static_cast<int>((dy + 0.5) * h / outH), 0, h - 1);
        for (int dx = 0; dx < outW; ++dx)
        {
            int srcX = std::clamp(static_cast<int>((dx + 0.5) * w / outW), 0, w - 1);
            out[dy * outW + dx] = src[srcY * w + srcX];
        }
    }

    return out;
}

ResampledImage resampleToDisplay(
    const RenderedSlice& slice, double aspect, int maxW, int maxH, int scale)
{
    ResampledImage result;

    if (slice.width <= 0 || slice.height <= 0)
        return result;

    int clampedScale = std::max(1, scale);
    int fitW = std::max(1, maxW / clampedScale);
    int fitH = std::max(1, maxH / clampedScale);

    auto size = computeResampleSize(slice.width, slice.height, aspect, fitW, fitH);
    result.pixels = resamplePixelsNearest(slice.pixels.data(), slice.width, slice.height,
                                           size.width, size.height);
    result.width  = size.width;
    result.height = size.height;

    if (clampedScale > 1)
    {
        int outW = result.width * clampedScale;
        int outH = result.height * clampedScale;
        result.pixels = resamplePixelsNearest(result.pixels.data(), result.width, result.height,
                                               outW, outH);
        result.width  = outW;
        result.height = outH;
    }

    return result;
}

int mapNativeToDisplay(int nativeCoord, int nativeSize, int displaySize)
{
    if (nativeSize <= 0 || displaySize <= 0)
        return 0;

    int mapped = static_cast<int>((nativeCoord + 0.5) * displaySize / nativeSize);
    return std::clamp(mapped, 0, displaySize - 1);
}

} // namespace mriv::term
