/// test_compose.cpp — compositing the per-cell slice images into the single
/// RGBA buffer that gets blitted.
///
/// The whole grid travels to the terminal as one bitmap: that keeps the
/// notcurses wrappers free of plane bookkeeping and makes the layout
/// testable without a terminal at all. Which means this function is where a
/// misplaced pane would come from, so placement is pinned exactly.

#include <cassert>

#include "render/Compose.hpp"

using namespace mriv::term;

namespace
{

constexpr uint32_t kBlack = 0xff000000u;
constexpr uint32_t kRed   = 0xff0000ffu; // 0xAABBGGRR
constexpr uint32_t kBlue  = 0xffff0000u;

ResampledImage solid(int w, int h, uint32_t colour)
{
    ResampledImage image;
    image.width  = w;
    image.height = h;
    image.pixels.assign(static_cast<size_t>(w) * h, colour);
    return image;
}

uint32_t pixelAt(const ResampledImage& image, int x, int y)
{
    return image.pixels[static_cast<size_t>(y) * image.width + x];
}

/// Anything not covered by a cell image is opaque black, not transparent
/// and not uninitialised: a terminal blitting an alpha-zero pixel shows
/// whatever was underneath, which on a redraw is the previous frame.
void testEmptyGridIsOpaqueBlack()
{
    auto canvas = composeGrid({}, 4, 3);
    assert(canvas.width == 4 && canvas.height == 3);
    assert(canvas.pixels.size() == 12);
    for (uint32_t pixel : canvas.pixels)
        assert(pixel == kBlack);
}

/// An image smaller than its cell is centred in it, so a narrow sagittal
/// pane sits in the middle of its column instead of hugging the left edge.
void testImageIsCentredInItsCell()
{
    auto image = solid(2, 2, kRed);
    std::vector<PlacedImage> placed{{&image, CellRect{0, 0, 6, 4}}};

    auto canvas = composeGrid(placed, 6, 4);
    assert(canvas.width == 6 && canvas.height == 4);

    // (6-2)/2 = 2 across, (4-2)/2 = 1 down.
    assert(pixelAt(canvas, 2, 1) == kRed);
    assert(pixelAt(canvas, 3, 2) == kRed);
    // Just outside the image on every side.
    assert(pixelAt(canvas, 1, 1) == kBlack);
    assert(pixelAt(canvas, 4, 1) == kBlack);
    assert(pixelAt(canvas, 2, 0) == kBlack);
    assert(pixelAt(canvas, 2, 3) == kBlack);
}

/// Two cells must not bleed into each other: the second column's pixels
/// stay in the second column.
void testCellsAreIndependent()
{
    auto left  = solid(2, 2, kRed);
    auto right = solid(2, 2, kBlue);
    std::vector<PlacedImage> placed{
        {&left,  CellRect{0, 0, 2, 2}},
        {&right, CellRect{2, 0, 2, 2}},
    };

    auto canvas = composeGrid(placed, 4, 2);
    assert(pixelAt(canvas, 0, 0) == kRed);
    assert(pixelAt(canvas, 1, 1) == kRed);
    assert(pixelAt(canvas, 2, 0) == kBlue);
    assert(pixelAt(canvas, 3, 1) == kBlue);
}

/// A cell at a non-zero offset lands at that offset, not at the origin.
void testCellOffsetIsHonoured()
{
    auto image = solid(1, 1, kRed);
    std::vector<PlacedImage> placed{{&image, CellRect{3, 2, 1, 1}}};

    auto canvas = composeGrid(placed, 5, 4);
    assert(pixelAt(canvas, 3, 2) == kRed);
    assert(pixelAt(canvas, 0, 0) == kBlack);
}

/// An image larger than its cell is clipped rather than allowed to run over
/// its neighbour or past the end of the buffer.
void testOversizedImageIsClipped()
{
    auto image = solid(10, 10, kRed);
    std::vector<PlacedImage> placed{{&image, CellRect{0, 0, 2, 2}}};

    auto canvas = composeGrid(placed, 4, 2);
    assert(canvas.pixels.size() == 8);
    assert(pixelAt(canvas, 0, 0) == kRed);
    assert(pixelAt(canvas, 1, 1) == kRed);
    // The neighbouring cell's area is untouched.
    assert(pixelAt(canvas, 2, 0) == kBlack);
    assert(pixelAt(canvas, 3, 1) == kBlack);
}

/// A cell whose render failed (an empty image, as renderSliceForDisplay()
/// returns for an empty volume) leaves its area black instead of aborting
/// the whole frame.
void testNullAndEmptyImagesAreSkipped()
{
    ResampledImage empty;
    auto image = solid(1, 1, kRed);
    std::vector<PlacedImage> placed{
        {nullptr, CellRect{0, 0, 1, 1}},
        {&empty,  CellRect{1, 0, 1, 1}},
        {&image,  CellRect{2, 0, 1, 1}},
    };

    auto canvas = composeGrid(placed, 3, 1);
    assert(pixelAt(canvas, 0, 0) == kBlack);
    assert(pixelAt(canvas, 1, 0) == kBlack);
    assert(pixelAt(canvas, 2, 0) == kRed);
}

void testDegenerateCanvasIsEmpty()
{
    assert(composeGrid({}, 0, 5).pixels.empty());
    assert(composeGrid({}, 5, 0).pixels.empty());
    assert(composeGrid({}, -1, 5).width == 0);
}

} // namespace

int main()
{
    testEmptyGridIsOpaqueBlack();
    testImageIsCentredInItsCell();
    testCellsAreIndependent();
    testCellOffsetIsHonoured();
    testOversizedImageIsClipped();
    testNullAndEmptyImagesAreSkipped();
    testDegenerateCanvasIsEmpty();
    return 0;
}
