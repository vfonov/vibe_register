/// test_crosshair.cpp — drawing the interactive cursor's position into a
/// rendered slice: the terminal-side equivalent of new_register's
/// ImDrawList crosshair overlay (Interface.cpp), baked into the RGBA
/// buffer instead of drawn on top of a texture.

#include <cassert>

#include "render/Crosshair.hpp"
#include "render/Resample.hpp"

using namespace mriv::term;

namespace
{

constexpr uint32_t kBackground = 0xFF202020u; // opaque dark gray

ResampledImage makeImage(int w, int h, uint32_t fill)
{
    ResampledImage image;
    image.width  = w;
    image.height = h;
    image.pixels.assign(static_cast<size_t>(w) * h, fill);
    return image;
}

uint32_t pixelAt(const ResampledImage& image, int x, int y)
{
    return image.pixels[static_cast<size_t>(y) * image.width + x];
}

/// The mark lands on the pixel mapNativeToDisplay() (render/Resample.hpp)
/// would give for its (u, v) -- the same nearest-neighbour mapping
/// resampleToDisplay() used to produce the image in the first place -- with
/// v flipped, matching renderSlice()'s bottom-up row order (PLAN.md).
void testDrawCrosshairMarksTheMappedRowAndColumn()
{
    auto image = makeImage(10, 10, kBackground);
    CrosshairMark mark{5, 5, 10, 10};

    drawCrosshair(image, mark);

    int col = mapNativeToDisplay(5, 10, 10);
    int row = mapNativeToDisplay(10 - 1 - 5, 10, 10);

    for (int x = 0; x < image.width; ++x)
        assert(pixelAt(image, x, row) != kBackground);
    for (int y = 0; y < image.height; ++y)
        assert(pixelAt(image, col, y) != kBackground);
}

/// A pixel off both the marked row and column is untouched.
void testDrawCrosshairLeavesOtherPixelsAlone()
{
    auto image = makeImage(10, 10, kBackground);
    CrosshairMark mark{2, 2, 10, 10};

    drawCrosshair(image, mark);

    int col = mapNativeToDisplay(2, 10, 10);
    int row = mapNativeToDisplay(10 - 1 - 2, 10, 10);
    int x = (col == 8) ? 7 : 8;
    int y = (row == 8) ? 7 : 8;
    assert(pixelAt(image, x, y) == kBackground);
}

/// Blended, not an opaque overwrite -- the slice underneath the line has to
/// stay legible, the same restraint new_register's ~39%-alpha crosshair
/// takes (Interface.cpp).
void testDrawCrosshairBlendsRatherThanOverwrites()
{
    auto image = makeImage(4, 4, 0xFF000000u); // opaque black
    CrosshairMark mark{1, 1, 4, 4};

    drawCrosshair(image, mark);

    int col = mapNativeToDisplay(1, 4, 4);
    int row = mapNativeToDisplay(4 - 1 - 1, 4, 4);
    uint32_t marked = pixelAt(image, col, row);
    assert(marked != 0xFF000000u);
    assert((marked & 0xFFu) < 0xFFu);          // not full-intensity red
    assert(((marked >> 24) & 0xFFu) == 0xFFu); // stays fully opaque
}

/// A coordinate outside the native size is clamped, not indexed out of the
/// pixel buffer.
void testDrawCrosshairClampsOutOfRangeCoordinates()
{
    auto image = makeImage(10, 10, kBackground);
    CrosshairMark mark{999, -5, 10, 10};
    drawCrosshair(image, mark); // must not crash
}

/// A degenerate image or native size is a no-op, not a crash.
void testDrawCrosshairIsANoOpOnDegenerateInput()
{
    ResampledImage empty;
    drawCrosshair(empty, CrosshairMark{0, 0, 10, 10});

    auto image = makeImage(4, 4, kBackground);
    drawCrosshair(image, CrosshairMark{0, 0, 0, 0});
    for (auto p : image.pixels)
        assert(p == kBackground);
}

} // namespace

int main()
{
    testDrawCrosshairMarksTheMappedRowAndColumn();
    testDrawCrosshairLeavesOtherPixelsAlone();
    testDrawCrosshairBlendsRatherThanOverwrites();
    testDrawCrosshairClampsOutOfRangeCoordinates();
    testDrawCrosshairIsANoOpOnDegenerateInput();
    return 0;
}
