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
    auto size = computeResampleSize(229, 96, 0.5, 320, 200);
    assert(size.width == 239);
    assert(size.height == 200);
}

void testComputeResampleSize_coronalIsSquare()
{
    // 64 x 96 slice, aspect 1.5 -> effective 96 x 96 (square), box 320x200
    // (deliberately non-square, so the squareness isn't an artifact of the box).
    auto size = computeResampleSize(64, 96, 1.5, 320, 200);
    assert(size.width == size.height);
    assert(size.width == 200);
}

void testComputeResampleSize_isotropicPreservesRatio()
{
    // aspect 1.0 is a no-op: output ratio must equal input ratio.
    auto size = computeResampleSize(100, 50, 1.0, 320, 200);
    assert(size.width == 320);
    assert(size.height == 160);
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
    return 0;
}
