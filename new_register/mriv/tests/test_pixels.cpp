/// test_pixels.cpp — Layer B decoded-pixel correctness tests.
///
/// Builds synthetic volumes in memory, renders slices through the parent's
/// renderSlice(), encodes them to Kitty PNG, decodes the bytes back to
/// RGBA, and asserts on image properties.

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <vector>

#include "decode.hpp"
#include "escapes.hpp"
#include "render/Encode.hpp"
#include "render/PixelProtocol.hpp"
#include "render/Resample.hpp"
#include "render/SliceGeometry.hpp"
#include "render/Terminal.hpp"

#include "ColourMap.h"
#include "SliceRenderer.h"
#include "Volume.h"

using namespace mriv::term;
using namespace mriv::term::test;

namespace
{

// Pack B,G,R,A bytes into the parent's 0xAABBGGRR convention (R in LSByte).
uint32_t packRgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(g) << 8)  |
           (static_cast<uint32_t>(r));
}

// Create a small volume whose voxel values vary linearly along `axis`:
// 0 at the first voxel, increasing to 1 at the last voxel.
Volume makeGradientVolume(int dimX, int dimY, int dimZ, int axis)
{
    Volume vol;
    vol.dimensions = glm::ivec3(dimX, dimY, dimZ);
    vol.step       = glm::dvec3(1.0, 1.0, 1.0);
    vol.start      = glm::dvec3(0.0, 0.0, 0.0);
    vol.dirCos     = glm::dmat3(1.0);
    vol.voxelToWorld = glm::dmat4(1.0);
    vol.worldToVoxel = glm::dmat4(1.0);
    vol.data.resize(static_cast<std::size_t>(dimX) * dimY * dimZ);
    vol.min_value = 0.0f;
    vol.max_value = 1.0f;

    int size = (axis == 0 ? dimX : (axis == 1 ? dimY : dimZ));
    for (int z = 0; z < dimZ; ++z)
    {
        for (int y = 0; y < dimY; ++y)
        {
            for (int x = 0; x < dimX; ++x)
            {
                int coord = (axis == 0 ? x : (axis == 1 ? y : z));
                float value = static_cast<float>(coord) / static_cast<float>(size - 1);
                vol.data[z * dimY * dimX + y * dimX + x] = value;
            }
        }
    }
    return vol;
}

// Render + encode a slice, then decode it back.
DecodedImage roundTripSlice(const Volume& vol,
                            int viewIndex,
                            int sliceIndex,
                            int maxWidth = 4096,
                            int maxHeight = 4096)
{
    VolumeRenderParams params;
    params.valueMin = 0.0;
    params.valueMax = 1.0;
    params.colourMap = ColourMapType::GrayScale;

    RenderedSlice slice = renderSlice(vol, params, viewIndex, sliceIndex);
    assert(slice.width > 0 && slice.height > 0);

    auto axes     = aspectAxesForView(viewIndex);
    double aspect = vol.slicePixelAspect(axes.u, axes.v);
    auto display  = resampleToDisplay(slice, aspect, maxWidth, maxHeight);

    std::ostringstream out;
    {
        Terminal term(out, PixelProtocol::Kitty);
        term.blit(display.pixels.data(), display.width, display.height);
    }

    auto events = parseEscapeStream(out.str());
    assert(events.size() == 1);
    assert(events[0].kind == EventKind::KittyGraphics);

    DecodedImage img = decodeKittyEvent(events[0]);
    if (img.width != display.width || img.height != display.height)
    {
        std::cerr << "decoded " << img.width << "x" << img.height
                  << " expected " << display.width << "x" << display.height
                  << " payload=" << events[0].payload.size() << "\n";
    }
    assert(img.width == display.width);
    assert(img.height == display.height);
    assert(!img.rgba.empty());
    return img;
}

std::uint8_t grey(const DecodedImage& img, int x, int y)
{
    std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4;
    return img.rgba[i]; // R channel after stb decode (top-down)
}

void testAxialMidSliceOfZGradientIsUniform()
{
    // Z-gradient: axial (viewIndex 0) middle slice is constant intensity.
    Volume vol = makeGradientVolume(16, 16, 16, 2);
    DecodedImage img = roundTripSlice(vol, 0, 8);
    assert(img.width == 16 && img.height == 16);

    std::uint8_t first = grey(img, 0, 0);
    for (int y = 0; y < img.height; ++y)
    {
        for (int x = 0; x < img.width; ++x)
        {
            assert(grey(img, x, y) == first);
        }
    }
}

void testSagittalMidSliceOfYGradientHasHorizontalGradient()
{
    // Sagittal (viewIndex 1) displays Y (columns) vs Z (rows). A Y-gradient
    // therefore produces a horizontal intensity gradient in the decoded image.
    Volume vol = makeGradientVolume(16, 16, 16, 1);
    DecodedImage img = roundTripSlice(vol, 1, 8);

    unsigned long leftSum = 0, rightSum = 0;
    for (int y = 0; y < img.height; ++y)
    {
        leftSum  += grey(img, 0, y);
        rightSum += grey(img, img.width - 1, y);
    }
    assert(rightSum > leftSum + 100); // materially brighter on the right
}

void testCheckerboardNativeSizeDecodesToCheckerboard()
{
    // 8x8x1 volume with alternating 0/1 pattern in X/Y.
    Volume vol;
    vol.dimensions = glm::ivec3(8, 8, 1);
    vol.step       = glm::dvec3(1.0, 1.0, 1.0);
    vol.start      = glm::dvec3(0.0, 0.0, 0.0);
    vol.dirCos     = glm::dmat3(1.0);
    vol.voxelToWorld = glm::dmat4(1.0);
    vol.worldToVoxel = glm::dmat4(1.0);
    vol.data.resize(64);
    vol.min_value = 0.0f;
    vol.max_value = 1.0f;
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            vol.data[y * 8 + x] = ((x + y) % 2 == 0) ? 1.0f : 0.0f;
        }
    }

    DecodedImage img = roundTripSlice(vol, 0, 0);
    assert(img.width == 8 && img.height == 8);

    int transitions = 0;
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 1; x < 8; ++x)
        {
            if (grey(img, x, y) != grey(img, x - 1, y))
                ++transitions;
        }
    }
    // A checkerboard row has 7 transitions; 8 rows -> 56, give or take
    // clamping/rounding at the 0.5 boundary.
    assert(transitions >= 48);
}

void testAutoWindowNonDegenerate()
{
    // A uniform slice with auto-window should produce a valid (if flat) image.
    Volume vol; // default generate_test_data() is a 256^3 isotropic volume.
    vol.generate_test_data();
    DecodedImage img = roundTripSlice(vol, 0, 128);

    unsigned long sum = 0;
    for (auto b : img.rgba)
        sum += b;
    double mean = static_cast<double>(sum) / img.rgba.size();
    assert(mean > 0.0);
    assert(mean < 255.0);
}

void testMaxWidthCapRespected()
{
    Volume vol = makeGradientVolume(64, 64, 16, 2);
    DecodedImage img = roundTripSlice(vol, 0, 8, 32, 4096);
    assert(img.width <= 32);
}

} // namespace

int main()
{
    testAxialMidSliceOfZGradientIsUniform();
    testSagittalMidSliceOfYGradientHasHorizontalGradient();
    testCheckerboardNativeSizeDecodesToCheckerboard();
    testAutoWindowNonDegenerate();
    testMaxWidthCapRespected();

    return 0;
}
