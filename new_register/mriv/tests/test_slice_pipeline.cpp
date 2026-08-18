/// test_slice_pipeline.cpp — the shared Volume -> display-image step.
///
/// renderSliceForDisplay() is the seam between the one-shot CLI path and the
/// interactive loop: both need the same slice-render + aspect-correct
/// resample, and neither should own a private copy of it. These tests pin
/// the behaviour that seam is responsible for -- aspect correction, the
/// display box, --scale, and empty input -- on synthetic volumes, so no
/// fixture file or terminal is needed.

#include <cassert>

#include "render/SlicePipeline.hpp"

#include "Volume.h"

using namespace mriv::term;

namespace
{

/// A volume with a deliberately anisotropic voxel step: 1 mm in X and Y,
/// 3 mm in Z. A coronal slice (viewIndex 2, in-plane axes X and Z) is
/// therefore three times taller in world space than its voxel count
/// suggests, which is the correction HANDOFF sec 5.3 warns must not be
/// skipped.
Volume makeAnisotropicVolume()
{
    Volume vol;
    vol.dimensions = glm::ivec3(20, 20, 20);
    vol.step       = glm::dvec3(1.0, 1.0, 3.0);
    vol.start      = glm::dvec3(0.0, 0.0, 0.0);
    vol.dirCos     = glm::dmat3(1.0);
    vol.voxelToWorld = glm::dmat4(1.0);
    vol.worldToVoxel = glm::dmat4(1.0);
    vol.data.assign(20 * 20 * 20, 0.5f);
    vol.min_value = 0.0f;
    vol.max_value = 1.0f;
    return vol;
}

SliceRequest makeRequest()
{
    SliceRequest request;
    request.viewIndex  = 0;
    request.sliceIndex = 10;
    request.valueMin   = 0.0;
    request.valueMax   = 1.0;
    request.maxWidth   = 4096;
    request.maxHeight  = 4096;
    return request;
}

/// Isotropic in-plane axes: the display image keeps the slice's native
/// square shape. computeResampleSize() caps upscaling at 1.0, so a 20x20
/// slice stays 20x20 in a large display box.
void testAxialSliceOfAnisotropicVolumeStaysSquare()
{
    Volume vol = makeAnisotropicVolume();
    SliceRequest request = makeRequest();
    request.viewIndex = 0; // axial: in-plane axes X and Y, both 1 mm

    auto image = renderSliceForDisplay(vol, request);
    assert(image.width == 20);
    assert(image.height == 20);
    assert(image.pixels.size() == 400);
}

/// Coronal: in-plane axes X (1 mm) and Z (3 mm), so the slice covers
/// 20 mm across and 60 mm down and must be displayed 1:3, not square.
/// The correction shrinks the narrow axis rather than stretching the long
/// one, because computeResampleSize() caps the scale at 1.0 and never
/// upscales past native size. Without the correction the image would come
/// out 20x20 and every structure in it would look a third too wide.
void testCoronalSliceIsAspectCorrected()
{
    Volume vol = makeAnisotropicVolume();
    SliceRequest request = makeRequest();
    request.viewIndex = 2;

    auto image = renderSliceForDisplay(vol, request);
    assert(image.height == 20);
    assert(image.width == 7); // lround(20 / 3)
    assert(image.pixels.size() == 140);
}

/// The display box is a hard cap: a slice that does not fit is scaled down,
/// preserving the corrected aspect ratio.
void testDisplayBoxCapsTheImage()
{
    Volume vol = makeAnisotropicVolume();
    SliceRequest request = makeRequest();
    request.viewIndex = 2;
    request.maxHeight = 10;

    auto image = renderSliceForDisplay(vol, request);
    // The 7x20 corrected image halved to fit a height of 10.
    assert(image.height == 10);
    assert(image.width == 3);
}

/// --scale magnifies after the fit, so each display pixel becomes an NxN
/// block rather than the image being fitted to a larger box.
void testScaleMagnifiesTheFittedImage()
{
    Volume vol = makeAnisotropicVolume();
    SliceRequest request = makeRequest();
    request.scale = 3;

    auto image = renderSliceForDisplay(vol, request);
    assert(image.width == 60);
    assert(image.height == 60);
}

/// An empty volume yields an empty image rather than a crash or a
/// zero-dimension buffer the terminal layer would have to guard against
/// (HANDOFF sec 3.7: renderSlice() returns an empty struct here).
void testEmptyVolumeYieldsEmptyImage()
{
    Volume vol;
    SliceRequest request = makeRequest();
    request.sliceIndex = 0;

    auto image = renderSliceForDisplay(vol, request);
    assert(image.width == 0);
    assert(image.height == 0);
    assert(image.pixels.empty());
}

/// The intensity range reaches renderSlice(): a uniform volume rendered
/// with the value at the bottom of the range is dark, and with the value at
/// the top is bright. This is what the interactive '+'/'-' keys drive.
void testIntensityRangeReachesTheRenderer()
{
    Volume vol = makeAnisotropicVolume(); // every voxel is 0.5

    SliceRequest dark = makeRequest();
    dark.valueMin = 0.5;
    dark.valueMax = 10.0;
    auto darkImage = renderSliceForDisplay(vol, dark);

    SliceRequest bright = makeRequest();
    bright.valueMin = -10.0;
    bright.valueMax = 0.5;
    auto brightImage = renderSliceForDisplay(vol, bright);

    assert(!darkImage.pixels.empty() && !brightImage.pixels.empty());
    // Red channel is the low byte of the parent's 0xAABBGGRR packing.
    unsigned darkRed   = darkImage.pixels.front() & 0xFFu;
    unsigned brightRed = brightImage.pixels.front() & 0xFFu;
    assert(brightRed > darkRed);
}

} // namespace

int main()
{
    testAxialSliceOfAnisotropicVolumeStaysSquare();
    testCoronalSliceIsAspectCorrected();
    testDisplayBoxCapsTheImage();
    testScaleMagnifiesTheFittedImage();
    testEmptyVolumeYieldsEmptyImage();
    testIntensityRangeReachesTheRenderer();

    return 0;
}
