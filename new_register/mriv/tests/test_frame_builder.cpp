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

/// The frame is sized to its content, not to the box. The box is a budget:
/// slices are never upscaled, so a box-sized canvas would wrap a small
/// volume in a screenful of black and blit megabytes of it every keypress.
void testFrameIsSizedToItsContent()
{
    Volume vol = makeVolume(20, 20, 20, 1.0f);
    FrameRequest request;
    request.panes     = {makePane(vol, {10, 10, 10})};
    request.views     = {0, 1, 2};
    request.boxWidth  = 400;
    request.boxHeight = 300;
    request.gap       = 0;

    auto frame = buildFrame(request);
    assert(frame.width == 20);
    assert(frame.height == 60);
    assert(frame.pixels.size() == 20u * 60u);
}

/// The gutter goes between the rows and columns, never around the outside.
void testGapSitsBetweenPanesOnly()
{
    Volume vol = makeVolume(20, 20, 20, 1.0f);
    FrameRequest request;
    request.panes     = {makePane(vol, {10, 10})};
    request.views     = {0, 1};
    request.boxWidth  = 400;
    request.boxHeight = 300;
    request.gap       = 6;

    auto frame = buildFrame(request);
    assert(frame.width == 20);       // one column, no gutter
    assert(frame.height == 46);      // 20 + 6 + 20
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
    assert(frame.height == 60);
    assert(drawnPixelsInBand(frame, 0, 0, frame.width, 20) > 0);
    assert(drawnPixelsInBand(frame, 0, 20, frame.width, 40) > 0);
    assert(drawnPixelsInBand(frame, 0, 40, frame.width, 60) > 0);
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

    // One row gets the whole 120px budget; three rows get 40 each.
    assert(one.width == 120 && one.height == 120);
    assert(all.width == 40 && all.height == 120);
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
    // Two 20-wide columns of a 20x20x20 volume, no gutter.
    assert(frame.width == 40);
    assert(frame.height == 20);

    uint32_t left  = pixelAt(frame, 10, 10);
    uint32_t right = pixelAt(frame, 30, 10);
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
    assert(drawnPixelsInBand(frame, 0, 0, frame.width, frame.height) > 0);

    int widest = 0;
    for (int x = 0; x < frame.width; ++x)
        if (pixelAt(frame, x, frame.height / 2) != kOpaqueBlack)
            ++widest;
    int tallest = 0;
    for (int y = 0; y < frame.height; ++y)
        if (pixelAt(frame, frame.width / 2, y) != kOpaqueBlack)
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
    // The broken pane's column shrinks to a minimal track rather than
    // vanishing, so the surviving column stays put on the right.
    assert(frame.width == 21);
    assert(drawnPixelsInBand(frame, 0, 0, 1, frame.height) == 0);
    assert(drawnPixelsInBand(frame, 1, 0, 21, frame.height) > 0);
}

/// The grid's track geometry comes back with the frame. The overlay that
/// marks the active row and column has to point at real pixels, and only
/// buildFrame() knows where the panes landed: the tracks are sized by what
/// the fit produced, not by the budget the layout started from.
void testTracksReportWhereThePanesLanded()
{
    Volume vol = makeVolume(20, 20, 20, 1.0f);
    FrameRequest request;
    request.panes     = {makePane(vol, {10, 10}), makePane(vol, {10, 10})};
    request.views     = {0, 1};
    request.boxWidth  = 400;
    request.boxHeight = 300;
    request.gap       = 6;

    FrameTracks tracks;
    auto frame = buildFrame(request, &tracks);

    assert(tracks.columnX.size() == 2);
    assert(tracks.rowY.size() == 2);
    assert(tracks.columnX[0] == 0 && tracks.columnX[1] == 26);   // 20 + gap
    assert(tracks.columnWidths[0] == 20 && tracks.columnWidths[1] == 20);
    assert(tracks.rowY[0] == 0 && tracks.rowY[1] == 26);
    assert(tracks.rowHeights[0] == 20 && tracks.rowHeights[1] == 20);

    // The last track ends exactly at the frame's edge, so a marker centred
    // on it cannot point past the picture.
    assert(tracks.columnX.back() + tracks.columnWidths.back() == frame.width);
    assert(tracks.rowY.back() + tracks.rowHeights.back() == frame.height);
}

/// A frame that could not be built has no tracks either: an overlay planned
/// from stale ones would mark panes that are not on screen.
void testDegenerateRequestLeavesNoTracks()
{
    FrameTracks tracks;
    tracks.columnX = {99};

    FrameRequest noPanes;
    noPanes.views     = {0};
    noPanes.boxWidth  = 100;
    noPanes.boxHeight = 100;

    assert(buildFrame(noPanes, &tracks).pixels.empty());
    assert(tracks.columnX.empty());
    assert(tracks.rowY.empty());
}

/// A pane whose row carries a crosshair mark gets it drawn into that cell --
/// the join between ViewState::crosshairFor() and render/Crosshair.hpp,
/// exercised the way planFrame() actually wires it (FramePane::crosshairs,
/// parallel to sliceIndices).
void testCrosshairIsDrawnIntoItsPane()
{
    Volume vol = makeVolume(20, 20, 20, 1.0f);

    FramePane withMark = makePane(vol, {10});
    withMark.crosshairs = {glm::ivec2(10, 10)};

    FramePane without = makePane(vol, {10});

    FrameRequest request;
    request.panes     = {withMark, without};
    request.views     = {0}; // axial: axes u=x, v=y
    request.boxWidth  = 200;
    request.boxHeight = 100;
    request.gap       = 0;

    auto frame = buildFrame(request);

    // Same volume, same slice, same colour map -- any pixel difference
    // between the two 20x20 columns is the crosshair, not the image.
    bool anyDifference = false;
    for (int y = 0; y < 20 && !anyDifference; ++y)
        for (int x = 0; x < 20; ++x)
            if (pixelAt(frame, x, y) != pixelAt(frame, x + 20, y))
            {
                anyDifference = true;
                break;
            }
    assert(anyDifference);
}

/// An empty crosshairs list -- what the one-shot render path always passes,
/// since it has no interactive cursor -- leaves the pane exactly as it
/// would be without the feature at all.
void testNoCrosshairMeansNoMark()
{
    Volume vol = makeVolume(20, 20, 20, 1.0f);

    FrameRequest request;
    request.panes     = {makePane(vol, {10})};
    request.views     = {0};
    request.boxWidth  = 200;
    request.boxHeight = 100;
    request.gap       = 0;

    auto plain = buildFrame(request);

    request.panes[0].crosshairs.clear();
    auto stillPlain = buildFrame(request);

    assert(plain.pixels == stillPlain.pixels);
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
    testFrameIsSizedToItsContent();
    testGapSitsBetweenPanesOnly();
    testEveryViewGetsARow();
    testSingleViewUsesTheWholeHeight();
    testVolumesBecomeColumnsInOrder();
    testEachPaneIsAspectCorrected();
    testMissingPaneDataLeavesTheCellBlank();
    testTracksReportWhereThePanesLanded();
    testDegenerateRequestLeavesNoTracks();
    testDegenerateRequestsYieldAnEmptyFrame();
    return 0;
}
