/// test_resample.cpp — aspect-correct nearest-neighbour resampling.
///
/// See mriv/HANDOFF.md sec 5.2/5.3: the aspect correction must be applied
/// before fitting the slice into the terminal's pixel box, or anisotropic
/// volumes render stretched.

#include <cassert>
#include <vector>

#include "render/Resample.hpp"

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
    return 0;
}
