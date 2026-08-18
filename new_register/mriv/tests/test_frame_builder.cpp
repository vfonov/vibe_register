/// test_frame_builder.cpp — the join between the slice pipeline, the grid
/// layout and the compositor.
///
/// buildFrame() is what both the one-shot path and the interactive loop
/// call, so it is the single place where "what the user sees" is decided.
/// These tests pin the frame's size, the pane ordering, and the fact that
/// each pane still gets its own aspect correction -- on synthetic volumes,
/// so no fixture file or terminal is involved.

#include <cassert>

#include "render/FrameBuilder.hpp"

#include "Volume.h"

using namespace mriv::term;

namespace
{

constexpr uint32_t kOpaqueBlack = 0xff000000u;

/// A uniform volume whose intensity says which volume it is, so a pane can
/// be identified by the colour that lands in it.
Volume makeVolume(int nx, int ny, int nz, float value, glm::dvec3 step = glm::dvec3(1.0))
{
    Volume vol;
    vol.dimensions   = glm::ivec3(nx, ny, nz);
    vol.step         = step;
    vol.start        = glm::dvec3(0.0);
    vol.dirCos       = glm::dmat3(1.0);
    vol.voxelToWorld = glm::dmat4(1.0);
    vol.worldToVoxel = glm::dmat4(1.0);
    vol.data.assign(static_cast<size_t>(nx) * ny * nz, value);
    vol.min_value = 0.0f;
    vol.max_value = 1.0f;
    return vol;
}

FramePane makePane(const Volume& vol, std::vector<int> sliceIndices)
{
    FramePane pane;
    pane.volume       = &vol;
    pane.sliceIndices = std::move(sliceIndices);
    pane.valueMin     = 0.0;
    pane.valueMax     = 1.0;
    return pane;
}

uint32_t pixelAt(const ResampledImage& image, int x, int y)
{
    return image.pixels[static_cast<size_t>(y) * image.width + x];
}

/// Count pixels that are not the background, as a cheap "is there an image
/// in this half of the frame" probe.
int drawnPixelsInBand(const ResampledImage& frame, int x0, int y0, int x1, int y1)
{
    int count = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (pixelAt(frame, x, y) != kOpaqueBlack)
                ++count;
    return count;
}

/// The frame always fills the box it was given, whatever the panes look
/// like: the terminal blits one bitmap, and a short one would leave the
/// previous frame's rows on screen.
void testFrameFillsTheRequestedBox()
{
    Volume vol = makeVolume(20, 20, 20, 1.0f);
    FrameRequest request;
    request.panes     = {makePane(vol, {10, 10, 10})};
    request.views     = {0, 1, 2};
    request.boxWidth  = 400;
    request.boxHeight = 300;

    auto frame = buildFrame(request);
    assert(frame.width == 400);
    assert(frame.height == 300);
    assert(frame.pixels.size() == 400u * 300u);
}

/// Three views stacked vertically: every row band has something drawn in
/// it. A silently dropped view would leave one band empty.
void testEveryViewGetsARow()
{
    Volume vol = makeVolume(20, 20, 20, 1.0f);
    FrameRequest request;
    request.panes     = {makePane(vol, {10, 10, 10})};
    request.views     = {0, 1, 2};
    request.boxWidth  = 120;
    request.boxHeight = 120;
    request.gap       = 0;

    auto frame = buildFrame(request);
    assert(drawnPixelsInBand(frame, 0, 0, 120, 40) > 0);
    assert(drawnPixelsInBand(frame, 0, 40, 120, 80) > 0);
    assert(drawnPixelsInBand(frame, 0, 80, 120, 120) > 0);
}

/// One view means one row filling the height, not a third of it.
///
/// The volume has to be bigger than the box for this to be observable:
/// computeResampleSize() caps the fit at 1.0, so a small slice sits at its
/// native size in a cell of any height and the row count makes no
/// difference to it.
void testSingleViewUsesTheWholeHeight()
{
    Volume vol = makeVolume(128, 128, 128, 1.0f);
    FrameRequest request;
    request.panes     = {makePane(vol, {10})};
    request.views     = {0};
    request.boxWidth  = 120;
    request.boxHeight = 120;
    request.gap       = 0;

    auto three = request;
    three.views = {0, 1, 2};
    three.panes = {makePane(vol, {10, 10, 10})};


    auto one = buildFrame(request);
    auto all = buildFrame(three);

    // The single pane is taller than any one pane of the three-row frame.
    int oneDrawn   = drawnPixelsInBand(one, 0, 0, 120, 120);
    int threeDrawn = drawnPixelsInBand(all, 0, 0, 120, 40);
    assert(oneDrawn > threeDrawn);
}

/// Volumes become columns in argument order. Each volume here is a
/// different uniform intensity, so which column got which volume is
/// visible in the pixels.
void testVolumesBecomeColumnsInOrder()
{
    Volume dark   = makeVolume(20, 20, 20, 0.1f);
    Volume bright = makeVolume(20, 20, 20, 0.9f);

    FrameRequest request;
    request.panes     = {makePane(dark, {10}), makePane(bright, {10})};
    request.views     = {0};
    request.boxWidth  = 200;
    request.boxHeight = 100;
    request.gap       = 0;

    auto frame = buildFrame(request);
    assert(frame.width == 200);

    uint32_t left  = pixelAt(frame, 50, 50);
    uint32_t right = pixelAt(frame, 150, 50);
    assert(left != kOpaqueBlack);
    assert(right != kOpaqueBlack);
    // Grayscale: the brighter volume's column is the brighter one, and it
    // is the second argument, so it is on the right.
    assert((right & 0xffu) > (left & 0xffu));
}

/// Each pane keeps its own aspect correction: a pane fed an anisotropic
/// volume is not silently stretched to its cell. 1 mm in X/Y, 3 mm in Z
/// means a coronal slice (in-plane X and Z) is 1:3.
void testEachPaneIsAspectCorrected()
{
    Volume anisotropic = makeVolume(20, 20, 20, 1.0f, glm::dvec3(1.0, 1.0, 3.0));

    FrameRequest request;
    request.panes     = {makePane(anisotropic, {10})};
    request.views     = {2}; // coronal
    request.boxWidth  = 300;
    request.boxHeight = 300;
    request.gap       = 0;

    auto frame = buildFrame(request);

    // The drawn region is three times taller than it is wide, centred.
    int drawn = drawnPixelsInBand(frame, 0, 0, 300, 300);
    assert(drawn > 0);

    int widest = 0;
    for (int x = 0; x < 300; ++x)
        if (pixelAt(frame, x, 150) != kOpaqueBlack)
            ++widest;
    int tallest = 0;
    for (int y = 0; y < 300; ++y)
        if (pixelAt(frame, 150, y) != kOpaqueBlack)
            ++tallest;

    // 20 voxels across x 20 down, corrected by aspect 1/3: the narrow axis
    // shrinks (the fit is capped at 1.0, so nothing is stretched up), giving
    // lround(20/3) = 7 by 20. Same numbers as test_slice_pipeline's
    // testCoronalSliceIsAspectCorrected -- the point here is that a pane
    // inside a grid still gets them.
    assert(widest == 7);
    assert(tallest == 20);
}

/// A pane whose slice list is short, or whose volume is missing, leaves its
/// cell blank rather than costing the whole frame.
void testMissingPaneDataLeavesTheCellBlank()
{
    Volume vol = makeVolume(20, 20, 20, 1.0f);

    FramePane broken;
    broken.volume = nullptr;

    FrameRequest request;
    request.panes     = {broken, makePane(vol, {10})};
    request.views     = {0};
    request.boxWidth  = 200;
    request.boxHeight = 100;
    request.gap       = 0;

    auto frame = buildFrame(request);
    assert(drawnPixelsInBand(frame, 0, 0, 100, 100) == 0);
    assert(drawnPixelsInBand(frame, 100, 0, 200, 100) > 0);
}

void testDegenerateRequestsYieldAnEmptyFrame()
{
    Volume vol = makeVolume(20, 20, 20, 1.0f);
    FrameRequest request;
    request.panes     = {makePane(vol, {10})};
    request.views     = {0};
    request.boxWidth  = 0;
    request.boxHeight = 100;
    assert(buildFrame(request).pixels.empty());

    FrameRequest noPanes;
    noPanes.views     = {0};
    noPanes.boxWidth  = 100;
    noPanes.boxHeight = 100;
    assert(buildFrame(noPanes).pixels.empty());

    FrameRequest noViews;
    noViews.panes     = {makePane(vol, {10})};
    noViews.boxWidth  = 100;
    noViews.boxHeight = 100;
    assert(buildFrame(noViews).pixels.empty());
}

} // namespace

int main()
{
    testFrameFillsTheRequestedBox();
    testEveryViewGetsARow();
    testSingleViewUsesTheWholeHeight();
    testVolumesBecomeColumnsInOrder();
    testEachPaneIsAspectCorrected();
    testMissingPaneDataLeavesTheCellBlank();
    testDegenerateRequestsYieldAnEmptyFrame();
    return 0;
}
