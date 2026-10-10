#include "render/Crosshair.hpp"

#include <cmath>
#include <cstdint>

namespace mriv::term
{

namespace
{

// Same colour and ~39% alpha as new_register's ImGui crosshair
// (IM_COL32(255, 255, 0, 100), Interface.cpp) -- yellow, translucent
// enough that the slice underneath stays legible. Packed 0xAABBGGRR:
// A=FF, B=00, G=FF, R=FF.
constexpr uint32_t kCrosshairColour = 0xFF00FFFFu;
constexpr double kCrosshairAlpha = 100.0 / 255.0;

uint32_t blendChannel(uint32_t bg, uint32_t mark, int shift, double alpha)
{
    double bgC   = static_cast<double>((bg >> shift) & 0xFFu);
    double markC = static_cast<double>((mark >> shift) & 0xFFu);
    return static_cast<uint32_t>(std::lround(bgC * (1.0 - alpha) + markC * alpha)) & 0xFFu;
}

uint32_t blendPixel(uint32_t bg)
{
    uint32_t r = blendChannel(bg, kCrosshairColour, 0, kCrosshairAlpha);
    uint32_t g = blendChannel(bg, kCrosshairColour, 8, kCrosshairAlpha);
    uint32_t b = blendChannel(bg, kCrosshairColour, 16, kCrosshairAlpha);
    uint32_t a = (bg >> 24) & 0xFFu; // stays exactly as opaque as the slice already was
    return (a << 24) | (b << 16) | (g << 8) | r;
}

} // namespace

void drawCrosshair(ResampledImage& image, const CrosshairMark& mark)
{
    if (image.width <= 0 || image.height <= 0)
        return;
    if (mark.nativeW <= 0 || mark.nativeH <= 0)
        return;

    // What the image shows of the slice: all of it, unless a zoom cropped
    // it. An image that never recorded a window is the whole slice.
    SourceWindow window = image.window;
    if (window.width <= 0.0 || window.height <= 0.0)
        window = SourceWindow{0.0, 0.0, static_cast<double>(mark.nativeW),
                              static_cast<double>(mark.nativeH)};

    // Off an edge of this volume, a line would claim a position it does not
    // have, so it is left out; the other line can still be in range. The
    // same goes for a line the zoomed window does not reach.
    int row = -1;
    if (mark.v >= 0 && mark.v < mark.nativeH)
        row = mapSourceToWindow(mark.nativeH - 1 - mark.v, window.y, window.height, image.height);
    if (row >= 0)
    {
        for (int x = 0; x < image.width; ++x)
            image.pixels[static_cast<size_t>(row) * image.width + x] =
                blendPixel(image.pixels[static_cast<size_t>(row) * image.width + x]);
    }

    int col = -1;
    if (mark.u >= 0 && mark.u < mark.nativeW)
        col = mapSourceToWindow(mark.u, window.x, window.width, image.width);
    if (col >= 0)
    {
        for (int y = 0; y < image.height; ++y)
            image.pixels[static_cast<size_t>(y) * image.width + col] =
                blendPixel(image.pixels[static_cast<size_t>(y) * image.width + col]);
    }
}

} // namespace mriv::term
