/// test_resample.cpp — aspect-correct nearest-neighbour resampling.
///
/// See mriv/HANDOFF.md sec 5.2/5.3: the aspect correction must be applied
/// before fitting the slice into the terminal's pixel box, or anisotropic
/// volumes render stretched.

#include <cassert>
#include <vector>

#include "render/Resample.hpp"
#include "render/SlicePipeline.hpp"

using namespace mriv::term;

namespace
{

void testComputeResampleSize_axial()
{
    // 64 x 229 slice, aspect 3.0 -> effective 192 x 229, box 320x200.
    auto size = computeResampleSize(64, 229, 3.0, 320, 200);
    assert(size.width == 168);
    assert(size.height == 200);
}

void testComputeResampleSize_sagittal()
{
    // 229 x 96 slice, aspect 0.5 -> effective 114.5 x 96, box 320x200.
    // The box is bigger than the effective size in both dimensions, so the
    // native-size cap (scale <= 1.0) applies: the slice is not upscaled,
    // it comes out at its own effective size, rounded.
    auto size = computeResampleSize(229, 96, 0.5, 320, 200);
    assert(size.width == 115);
    assert(size.height == 96);
}

void testComputeResampleSize_coronalIsSquare()
{
    // 64 x 96 slice, aspect 1.5 -> effective 96 x 96 (square), box 320x200
    // (deliberately non-square, so the squareness isn't an artifact of the box).
    // The box is bigger than the effective size, so the native-size cap
    // (scale <= 1.0) applies and the output stays at its own effective
    // size -- still square either way, which is the property under test.
    auto size = computeResampleSize(64, 96, 1.5, 320, 200);
    assert(size.width == size.height);
    assert(size.width == 96);
}

void testComputeResampleSize_isotropicPreservesRatio()
{
    // aspect 1.0 is a no-op: output ratio must equal input ratio. The box
    // (320x200) is bigger than the effective size (100x50) in both
    // dimensions, so the native-size cap applies and the slice passes
    // through at its own size, unscaled and undistorted.
    auto size = computeResampleSize(100, 50, 1.0, 320, 200);
    assert(size.width == 100);
    assert(size.height == 50);
}

void testComputeResampleSize_neverZero()
{
    // A degenerate box must not produce a 0-sized image.
    auto size = computeResampleSize(1000, 1000, 1.0, 0, 0);
    assert(size.width >= 1);
    assert(size.height >= 1);
}

void testResamplePixelsNearest_upsample()
{
    // 2x1 -> 4x1: [A,B] -> [A,A,B,B]
    const uint32_t A = 0xAAAAAAAA, B = 0xBBBBBBBB;
    std::vector<uint32_t> src{A, B};

    auto out = resamplePixelsNearest(src.data(), 2, 1, 4, 1);
    assert(out.size() == 4);
    assert(out[0] == A);
    assert(out[1] == A);
    assert(out[2] == B);
    assert(out[3] == B);
}

void testResamplePixelsNearest_downsample()
{
    // 4x1 -> 2x1: nearest-neighbour picks src[1] then src[3].
    std::vector<uint32_t> src{0, 1, 2, 3};

    auto out = resamplePixelsNearest(src.data(), 4, 1, 2, 1);
    assert(out.size() == 2);
    assert(out[0] == 1);
    assert(out[1] == 3);
}

void testResamplePixelsNearest_identity()
{
    std::vector<uint32_t> src{10, 20, 30, 40};
    auto out = resamplePixelsNearest(src.data(), 2, 2, 2, 2);
    assert(out == src);
}

// -R/--scale: an integer scale factor magnifies the resampled display image
// by replicating each pixel into a scale x scale block (nearest-neighbour),
// applied *after* the aspect-correct fit into (maxW, maxH) -- so scale=1
// must be a no-op and scale>1 must not exceed the box by more than
// (scale - 1) pixels in either dimension.

void testResampleToDisplay_scaleOneIsUnchanged()
{
    RenderedSlice slice;
    slice.width  = 2;
    slice.height = 2;
    slice.pixels = {1, 2, 3, 4};

    auto unscaled = resampleToDisplay(slice, 1.0, 320, 200);
    auto scaled1  = resampleToDisplay(slice, 1.0, 320, 200, 1);

    assert(scaled1.width == unscaled.width);
    assert(scaled1.height == unscaled.height);
    assert(scaled1.pixels == unscaled.pixels);
}

void testResampleToDisplay_scaleMagnifiesByReplication()
{
    // 2x2 slice, box far bigger than native size so the native-size cap
    // (PLAN.md/HANDOFF.md sec 5.3) keeps the pre-scale fit at 2x2; scale=3
    // must then replicate each pixel into a 3x3 block, giving 6x6 total.
    RenderedSlice slice;
    slice.width  = 2;
    slice.height = 2;
    slice.pixels = {0xAAAAAAAAu, 0xBBBBBBBBu, 0xCCCCCCCCu, 0xDDDDDDDDu};

    auto out = resampleToDisplay(slice, 1.0, 320, 200, 3);
    assert(out.width == 6);
    assert(out.height == 6);

    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            assert(out.pixels[y * out.width + x] == 0xAAAAAAAAu);

    for (int y = 0; y < 3; ++y)
        for (int x = 3; x < 6; ++x)
            assert(out.pixels[y * out.width + x] == 0xBBBBBBBBu);
}

void testResampleToDisplay_scaleRespectsBox()
{
    // The pre-scale fit must target (maxW/scale, maxH/scale), not
    // (maxW, maxH), so the magnified result never exceeds the terminal's
    // pixel box (beyond integer-division slop of scale-1 pixels).
    RenderedSlice slice;
    slice.width  = 1000;
    slice.height = 1000;
    slice.pixels.assign(1000u * 1000u, 0xFFFFFFFFu);

    auto out = resampleToDisplay(slice, 1.0, 100, 100, 4);
    assert(out.width <= 100);
    assert(out.height <= 100);
    assert(out.width % 4 == 0);
    assert(out.height % 4 == 0);
}

// mapNativeToDisplay(): the crosshair overlay (render/Crosshair.hpp) needs
// to place a mark at the pixel a native (pre-resample) coordinate ends up
// at after resamplePixelsNearest()'s scaling, without threading the
// two-stage fit-then-magnify internals of resampleToDisplay() through the
// caller. It is the same centre-of-source-pixel convention, inverted.

void testMapNativeToDisplay_identity()
{
    assert(mapNativeToDisplay(0, 4, 4) == 0);
    assert(mapNativeToDisplay(3, 4, 4) == 3);
}

void testMapNativeToDisplay_upsample()
{
    // 2 -> 4: native pixel 0 covers display pixels [0,1], pixel 1 covers [2,3].
    assert(mapNativeToDisplay(0, 2, 4) == 1);
    assert(mapNativeToDisplay(1, 2, 4) == 3);
}

void testMapNativeToDisplay_downsample()
{
    // 4 -> 2: native pixels 0,1 land on display pixel 0; 2,3 on pixel 1.
    assert(mapNativeToDisplay(0, 4, 2) == 0);
    assert(mapNativeToDisplay(3, 4, 2) == 1);
}

void testMapNativeToDisplay_clampsToTheLastPixel()
{
    assert(mapNativeToDisplay(9, 10, 10) == 9);
    assert(mapNativeToDisplay(999, 10, 10) == 9);
}

void testMapNativeToDisplay_degenerateSizesReturnZero()
{
    assert(mapNativeToDisplay(5, 0, 10) == 0);
    assert(mapNativeToDisplay(5, 10, 0) == 0);
}

// Zoom: the source is drawn zoom.factor times larger than the plain fit.
// The image grows until it fills the box; after that the box stays full and
// the source is cropped to a window around the zoom centre.

/// A w x h image whose pixel value is its own index, so a test can tell
/// which source pixel landed where.
std::vector<uint32_t> indexedPixels(int w, int h)
{
    std::vector<uint32_t> pixels(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < pixels.size(); ++i)
        pixels[i] = static_cast<uint32_t>(i);
    return pixels;
}

void testZoom_oneIsThePlainFit()
{
    auto src = indexedPixels(10, 10);

    auto plain  = resampleToDisplay(src.data(), 10, 10, 1.5, 7, 9);
    auto zoomed = resampleToDisplay(src.data(), 10, 10, 1.5, 7, 9, 1, ZoomView{1.0, 2.0, 3.0});

    assert(zoomed.width == plain.width);
    assert(zoomed.height == plain.height);
    assert(zoomed.pixels == plain.pixels);

    // Nothing was cropped, and the window says so.
    assert(zoomed.window.x == 0.0 && zoomed.window.y == 0.0);
    assert(zoomed.window.width == 10.0 && zoomed.window.height == 10.0);
    assert(plain.window.width == 10.0 && plain.window.height == 10.0);
}

void testZoom_enlargesWhileTheBoxHasRoom()
{
    // 4x4 in a box far bigger: zoom 2 draws it 8x8, every source pixel a
    // 2x2 block, nothing cropped. The plain fit never enlarges; zoom does.
    auto src = indexedPixels(4, 4);

    auto out = resampleToDisplay(src.data(), 4, 4, 1.0, 100, 100, 1, ZoomView{2.0});
    assert(out.width == 8);
    assert(out.height == 8);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            assert(out.pixels[y * 8 + x] == static_cast<uint32_t>((y / 2) * 4 + x / 2));

    assert(out.window.x == 0.0 && out.window.y == 0.0);
    assert(out.window.width == 4.0 && out.window.height == 4.0);
}

void testZoom_cropsOnceTheBoxIsFull()
{
    // 10x10 in a 10x10 box: zoom 2 cannot grow, so it shows the middle 5x5
    // of the source at twice the size.
    auto src = indexedPixels(10, 10);

    auto out = resampleToDisplay(src.data(), 10, 10, 1.0, 10, 10, 1, ZoomView{2.0});
    assert(out.width == 10);
    assert(out.height == 10);
    assert(out.window.x == 2.5 && out.window.y == 2.5);
    assert(out.window.width == 5.0 && out.window.height == 5.0);

    assert(out.pixels[0] == 2u * 10 + 2);  // source (2,2)
    assert(out.pixels[99] == 7u * 10 + 7); // source (7,7)
}

void testZoom_windowFollowsTheCentreAndStaysInside()
{
    auto src = indexedPixels(10, 10);

    // Centred on source pixel (6,3): the 5x5 window runs 4..9 and 1..6.
    auto out = resampleToDisplay(src.data(), 10, 10, 1.0, 10, 10, 1, ZoomView{2.0, 6.5, 3.5});
    assert(out.window.x == 4.0);
    assert(out.window.y == 1.0);

    // Near a corner the window slides back inside rather than showing
    // nothing beyond the edge.
    auto low = resampleToDisplay(src.data(), 10, 10, 1.0, 10, 10, 1, ZoomView{2.0, 0.5, 0.5});
    assert(low.window.x == 0.0 && low.window.y == 0.0);
    assert(low.pixels[0] == 0u);

    auto high = resampleToDisplay(src.data(), 10, 10, 1.0, 10, 10, 1, ZoomView{2.0, 9.5, 9.5});
    assert(high.window.x == 5.0 && high.window.y == 5.0);
    assert(high.pixels[99] == 99u);
}

void testZoom_belowOneShrinks()
{
    auto src = indexedPixels(10, 10);

    auto out = resampleToDisplay(src.data(), 10, 10, 1.0, 100, 100, 1, ZoomView{0.5});
    assert(out.width == 5);
    assert(out.height == 5);
    assert(out.window.width == 10.0 && out.window.height == 10.0);
}

void testZoom_keepsTheAspectCorrection()
{
    // Voxels twice as wide as tall: 10x10 is 20x10 on screen. In a 20x10
    // box zoom 2 crops to the 5x5 source window that fills it.
    auto src = indexedPixels(10, 10);

    auto out = resampleToDisplay(src.data(), 10, 10, 2.0, 20, 10, 1, ZoomView{2.0});
    assert(out.width == 20);
    assert(out.height == 10);
    assert(out.window.width == 5.0);
    assert(out.window.height == 5.0);
}

void testZoom_combinesWithScale()
{
    // --scale fits into box/scale and replicates; the zoom acts on that
    // fit, so the result is still a multiple of the scale and inside the
    // box.
    auto src = indexedPixels(10, 10);

    auto out = resampleToDisplay(src.data(), 10, 10, 1.0, 20, 20, 2, ZoomView{2.0});
    assert(out.width == 20);
    assert(out.height == 20);
    assert(out.window.x == 2.5 && out.window.width == 5.0);
    assert(out.pixels[0] == 22u);
    assert(out.pixels[1] == 22u); // replicated by the scale
}

void testMapSourceToWindow()
{
    // The whole source: the same answer mapNativeToDisplay() gives.
    assert(mapSourceToWindow(5, 0.0, 10.0, 10) == mapNativeToDisplay(5, 10, 10));
    assert(mapSourceToWindow(3, 0.0, 10.0, 20) == mapNativeToDisplay(3, 10, 20));

    // A 5-pixel window starting at 2.5, drawn 10 pixels wide.
    assert(mapSourceToWindow(5, 2.5, 5.0, 10) == 6);
    assert(mapSourceToWindow(3, 2.5, 5.0, 10) == 2);

    // Outside the window on either side: not on screen.
    assert(mapSourceToWindow(1, 2.5, 5.0, 10) == -1);
    assert(mapSourceToWindow(8, 2.5, 5.0, 10) == -1);

    assert(mapSourceToWindow(5, 0.0, 0.0, 10) == -1);
    assert(mapSourceToWindow(5, 0.0, 10.0, 0) == -1);
}

} // namespace

int main()
{
    testComputeResampleSize_axial();
    testComputeResampleSize_sagittal();
    testComputeResampleSize_coronalIsSquare();
    testComputeResampleSize_isotropicPreservesRatio();
    testComputeResampleSize_neverZero();
    testResamplePixelsNearest_upsample();
    testResamplePixelsNearest_downsample();
    testResamplePixelsNearest_identity();
    testResampleToDisplay_scaleOneIsUnchanged();
    testResampleToDisplay_scaleMagnifiesByReplication();
    testResampleToDisplay_scaleRespectsBox();
    testMapNativeToDisplay_identity();
    testMapNativeToDisplay_upsample();
    testMapNativeToDisplay_downsample();
    testMapNativeToDisplay_clampsToTheLastPixel();
    testMapNativeToDisplay_degenerateSizesReturnZero();
    testZoom_oneIsThePlainFit();
    testZoom_enlargesWhileTheBoxHasRoom();
    testZoom_cropsOnceTheBoxIsFull();
    testZoom_windowFollowsTheCentreAndStaysInside();
    testZoom_belowOneShrinks();
    testZoom_keepsTheAspectCorrection();
    testZoom_combinesWithScale();
    testMapSourceToWindow();
    return 0;
}
