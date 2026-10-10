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

namespace
{

/// The zoomed counterpart of computeResampleSize() + resamplePixelsNearest():
/// draw the source `zoom.factor` times larger than its plain fit into
/// (fitW, fitH), cropped to the box around the zoom centre.
ResampledImage resampleZoomed(
    const uint32_t* src, int w, int h, double aspect, int fitW, int fitH, const ZoomView& zoom)
{
    double effectiveW = w * aspect;
    double effectiveH = h;

    // Display pixels per effective source pixel: the plain fit's own
    // factor (never above 1), times the zoom.
    double fit = std::min(1.0, std::min(fitW / effectiveW, fitH / effectiveH));
    double pixelsPerUnit = fit * zoom.factor;

    ResampledImage result;
    result.width  = std::clamp(static_cast<int>(std::lround(effectiveW * pixelsPerUnit)), 1, fitW);
    result.height = std::clamp(static_cast<int>(std::lround(effectiveH * pixelsPerUnit)), 1, fitH);

    // How much of the source that many display pixels can show: all of it
    // until the box is full, a window of it after.
    SourceWindow& window = result.window;
    window.width  = std::min(static_cast<double>(w), result.width / (pixelsPerUnit * aspect));
    window.height = std::min(static_cast<double>(h), result.height / pixelsPerUnit);

    double centreX = zoom.centreX < 0.0 ? w / 2.0 : zoom.centreX;
    double centreY = zoom.centreY < 0.0 ? h / 2.0 : zoom.centreY;
    window.x = std::clamp(centreX - window.width / 2.0, 0.0, w - window.width);
    window.y = std::clamp(centreY - window.height / 2.0, 0.0, h - window.height);

    result.pixels.resize(static_cast<size_t>(result.width) * result.height);
    for (int dy = 0; dy < result.height; ++dy)
    {
        int srcY = std::clamp(
            static_cast<int>(window.y + (dy + 0.5) * window.height / result.height), 0, h - 1);
        for (int dx = 0; dx < result.width; ++dx)
        {
            int srcX = std::clamp(
                static_cast<int>(window.x + (dx + 0.5) * window.width / result.width), 0, w - 1);
            result.pixels[static_cast<size_t>(dy) * result.width + dx] =
                src[static_cast<size_t>(srcY) * w + srcX];
        }
    }

    return result;
}

} // namespace

ResampledImage resampleToDisplay(
    const uint32_t* src, int w, int h, double aspect, int maxW, int maxH, int scale,
    const ZoomView& zoom)
{
    ResampledImage result;

    if (!src || w <= 0 || h <= 0)
        return result;

    int clampedScale = std::max(1, scale);
    int fitW = std::max(1, maxW / clampedScale);
    int fitH = std::max(1, maxH / clampedScale);

    // Exactly 1 (and anything that is not a usable factor) takes the plain
    // fit, so unzoomed output is the same pixels it always was.
    if (zoom.factor == 1.0 || !(zoom.factor > 0.0))
    {
        auto size = computeResampleSize(w, h, aspect, fitW, fitH);
        result.pixels = resamplePixelsNearest(src, w, h, size.width, size.height);
        result.width  = size.width;
        result.height = size.height;
        result.window = SourceWindow{0.0, 0.0, static_cast<double>(w), static_cast<double>(h)};
    }
    else
    {
        result = resampleZoomed(src, w, h, aspect, fitW, fitH, zoom);
    }

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

int mapSourceToWindow(int sourceCoord, double windowStart, double windowSize, int displaySize)
{
    if (windowSize <= 0.0 || displaySize <= 0)
        return -1;

    double mapped = std::floor((sourceCoord + 0.5 - windowStart) * displaySize / windowSize);
    if (mapped < 0.0 || mapped >= displaySize)
        return -1;
    return static_cast<int>(mapped);
}

int mapNativeToDisplay(int nativeCoord, int nativeSize, int displaySize)
{
    if (nativeSize <= 0 || displaySize <= 0)
        return 0;

    int mapped = static_cast<int>((nativeCoord + 0.5) * displaySize / nativeSize);
    return std::clamp(mapped, 0, displaySize - 1);
}

} // namespace mriv::term
