/// test_slice_geometry.cpp — --axis -> viewIndex mapping and the
/// viewIndex -> slicePixelAspect() axis-pair table.
///
/// See mriv/HANDOFF.md sec 3.5 for the table this pins down: it is not
/// derivable by a simple formula, so a swapped pair here produces a
/// plausible-looking but wrong image on anisotropic data.

#include <cassert>

#include "render/SliceGeometry.hpp"

#include "Volume.h"

using namespace mriv::term;

namespace
{

void testAxisToViewIndex()
{
    assert(viewIndexForAxis('z').value() == 0);
    assert(viewIndexForAxis('x').value() == 1);
    assert(viewIndexForAxis('y').value() == 2);
    assert(!viewIndexForAxis('w').has_value());
    assert(!viewIndexForAxis('\0').has_value());
}

/// The number of slices along the axis a given view slices through.
/// Pinned here because a wrong entry silently truncates the navigable
/// range instead of failing: viewIndex 0 (axial) walks Z, 1 (sagittal)
/// walks X, 2 (coronal) walks Y.
void testSliceCountForView()
{
    glm::ivec3 dims{64, 229, 96};
    assert(sliceCountForView(0, dims) == 96);
    assert(sliceCountForView(1, dims) == 64);
    assert(sliceCountForView(2, dims) == 229);
}

/// Cross-volume slice synchronisation. The cursor lives in the first
/// volume's index space; every other volume tracks it proportionally.
/// Equal counts must be an exact identity -- the common case is the same
/// subject in two modalities, where "the same slice" has to mean the same
/// index, not one rounded off by a pixel.
void testMapSliceIndexIsIdentityForEqualCounts()
{
    for (int i = 0; i < 96; ++i)
        assert(mapSliceIndex(i, 96, 96) == i);
}

void testMapSliceIndexScalesProportionally()
{
    // Half the slices: index 50 of 100 lands mid-stack in a 50-slice volume.
    assert(mapSliceIndex(50, 100, 50) == 25);
    assert(mapSliceIndex(0, 100, 50) == 0);
    // Twice the slices. Note 51, not 50: the scaling is by last index, so
    // index 25 of 0..49 is 51% of the way up and lands at 51 of 0..99.
    // That is the cost of aligning the two ends exactly, which matters more
    // than a half-slice at the middle -- see testMapSliceIndexClampsAtBothEnds.
    assert(mapSliceIndex(25, 50, 100) == 51);
}

/// The last slice of the source must map to the last slice of the target,
/// not one past it: a cursor parked at the end of the reference volume
/// still has to address a real slice in a shorter one.
void testMapSliceIndexClampsAtBothEnds()
{
    assert(mapSliceIndex(99, 100, 50) == 49);
    assert(mapSliceIndex(49, 50, 100) == 99);
    assert(mapSliceIndex(1000, 100, 50) == 49);
    assert(mapSliceIndex(-5, 100, 50) == 0);
}

/// Degenerate counts must not divide by zero or return a negative index.
void testMapSliceIndexHandlesEmptyCounts()
{
    assert(mapSliceIndex(5, 0, 10) == 0);
    assert(mapSliceIndex(5, 10, 0) == 0);
    assert(mapSliceIndex(0, 1, 1) == 0);
}

void testAspectAxesTable()
{
    auto axial = aspectAxesForView(0);
    assert(axial.u == 0 && axial.v == 1);

    auto sagittal = aspectAxesForView(1);
    assert(sagittal.u == 1 && sagittal.v == 2);

    auto coronal = aspectAxesForView(2);
    assert(coronal.u == 0 && coronal.v == 2);
}

/// Integration check: apply our table to Volume::slicePixelAspect() on the
/// anisotropic fixture and confirm the exact values from HANDOFF.md sec 5.3.
void testAspectAxesAgainstThickSlicesVolume(const std::string& path)
{
    Volume vol;
    vol.load(path);

    assert(vol.dimensions.x == 64);
    assert(vol.dimensions.y == 229);
    assert(vol.dimensions.z == 96);

    auto axial = aspectAxesForView(0);
    double axialAspect = vol.slicePixelAspect(axial.u, axial.v);
    assert(axialAspect > 2.999 && axialAspect < 3.001);

    auto sagittal = aspectAxesForView(1);
    double sagittalAspect = vol.slicePixelAspect(sagittal.u, sagittal.v);
    assert(sagittalAspect > 0.499 && sagittalAspect < 0.501);

    auto coronal = aspectAxesForView(2);
    double coronalAspect = vol.slicePixelAspect(coronal.u, coronal.v);
    assert(coronalAspect > 1.499 && coronalAspect < 1.501);
}

} // namespace

int main(int argc, char** argv)
{
    (void)argc;
    const char* path = getenv("MRIV_THICK_SLICES_FIXTURE");
    assert(path && "MRIV_THICK_SLICES_FIXTURE must point to mni_icbm152_t1_tal_nlin_sym_09c_thick_slices.mnc");

    testAxisToViewIndex();
    testSliceCountForView();
    testMapSliceIndexIsIdentityForEqualCounts();
    testMapSliceIndexScalesProportionally();
    testMapSliceIndexClampsAtBothEnds();
    testMapSliceIndexHandlesEmptyCounts();
    testAspectAxesTable();
    testAspectAxesAgainstThickSlicesVolume(path);

    return 0;
}
