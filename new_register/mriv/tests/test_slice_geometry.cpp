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
    assert(argc == 2 && "usage: test_slice_geometry <thick_slices.mnc>");

    testAxisToViewIndex();
    testAspectAxesTable();
    testAspectAxesAgainstThickSlicesVolume(argv[1]);

    return 0;
}
