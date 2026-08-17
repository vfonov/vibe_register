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

ResampledImage resampleToDisplay(const RenderedSlice& slice, double aspect, int maxW, int maxH)
{
    ResampledImage result;

    if (slice.width <= 0 || slice.height <= 0)
        return result;

    auto size = computeResampleSize(slice.width, slice.height, aspect, maxW, maxH);
    result.pixels = resamplePixelsNearest(slice.pixels.data(), slice.width, slice.height,
                                           size.width, size.height);
    result.width  = size.width;
    result.height = size.height;

    return result;
}

} // namespace mriv::term
