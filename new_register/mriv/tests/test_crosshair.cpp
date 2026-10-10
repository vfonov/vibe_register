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

/// A coordinate outside the native size means the cursor is off that edge
/// of this volume: its line is left out rather than drawn along the edge,
/// which would claim a position the volume does not have. The other line
/// is still drawn if it is in range.
void testDrawCrosshairOmitsOutOfRangeLines()
{
    auto both = makeImage(10, 10, kBackground);
    drawCrosshair(both, CrosshairMark{999, -5, 10, 10});
    for (uint32_t pixel : both.pixels)
        assert(pixel == kBackground);

    // u out of range, v = 9 (the top native row, display row 0): only the
    // row is marked.
    auto rowOnly = makeImage(10, 10, kBackground);
    drawCrosshair(rowOnly, CrosshairMark{-1, 9, 10, 10});
    for (int x = 0; x < 10; ++x)
        assert(rowOnly.pixels[static_cast<size_t>(x)] != kBackground);
    for (int y = 1; y < 10; ++y)
        for (int x = 0; x < 10; ++x)
            assert(rowOnly.pixels[static_cast<size_t>(y) * 10 + x] == kBackground);

    // v out of range, u = 0: only the column is marked.
    auto columnOnly = makeImage(10, 10, kBackground);
    drawCrosshair(columnOnly, CrosshairMark{0, 10, 10, 10});
    for (int y = 0; y < 10; ++y)
    {
        assert(columnOnly.pixels[static_cast<size_t>(y) * 10] != kBackground);
        for (int x = 1; x < 10; ++x)
            assert(columnOnly.pixels[static_cast<size_t>(y) * 10 + x] == kBackground);
    }
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

/// A zoomed image shows only a window of its slice (ResampledImage::window);
/// the mark goes where the cursor is within that window, not where it would
/// be if the whole slice were on screen.
void testDrawCrosshairFollowsAZoomedWindow()
{
    auto image = makeImage(10, 10, kBackground);
    image.window = SourceWindow{2.5, 2.5, 5.0, 5.0};

    // u=5 is 3 source pixels into the window at 2 display pixels each; v=4
    // is native row 10-1-4 = 5, the same distance down.
    drawCrosshair(image, CrosshairMark{5, 4, 10, 10});

    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x)
            assert((pixelAt(image, x, y) != kBackground) == (x == 6 || y == 6));
}

/// A cursor inside the volume but outside the zoomed window has no place
/// on screen, so its line is left out -- not pinned to the nearest edge.
void testDrawCrosshairOmitsLinesOutsideTheWindow()
{
    auto image = makeImage(10, 10, kBackground);
    image.window = SourceWindow{2.5, 2.5, 5.0, 5.0};

    drawCrosshair(image, CrosshairMark{0, 4, 10, 10}); // u left of the window

    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x)
            assert((pixelAt(image, x, y) != kBackground) == (y == 6));
}

} // namespace

int main()
{
    testDrawCrosshairMarksTheMappedRowAndColumn();
    testDrawCrosshairLeavesOtherPixelsAlone();
    testDrawCrosshairBlendsRatherThanOverwrites();
    testDrawCrosshairOmitsOutOfRangeLines();
    testDrawCrosshairIsANoOpOnDegenerateInput();
    testDrawCrosshairFollowsAZoomedWindow();
    testDrawCrosshairOmitsLinesOutsideTheWindow();
    return 0;
}
