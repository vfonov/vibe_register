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

/// The reverse of viewIndexForAxis(), for reporting the active plane back
/// to the user and for falling back to a displayed view when the requested
/// --axis is not one of them.
void testAxisForViewIndex()
{
    assert(axisForViewIndex(0) == 'z');
    assert(axisForViewIndex(1) == 'x');
    assert(axisForViewIndex(2) == 'y');
    // Round trip through both directions.
    for (char axis : {'x', 'y', 'z'})
        assert(axisForViewIndex(viewIndexForAxis(axis).value()) == axis);
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

/// A geometry built the way the parent builds one (Volume.h:
/// world = dirCos * (start + index * step)), so the tests state volumes in
/// the same terms --info prints.
VolumeGeometry makeGeometry(const glm::ivec3& dims,
                            const glm::dvec3& step,
                            const glm::dvec3& start,
                            const glm::dmat3& dirCos = glm::dmat3(1.0))
{
    VolumeGeometry geometry;
    geometry.dimensions = dims;
    geometry.voxelToWorld = glm::dmat4(glm::dvec4(dirCos[0] * step.x, 0.0),
                                       glm::dvec4(dirCos[1] * step.y, 0.0),
                                       glm::dvec4(dirCos[2] * step.z, 0.0),
                                       glm::dvec4(dirCos * start, 1.0));
    geometry.worldToVoxel = glm::inverse(geometry.voxelToWorld);
    return geometry;
}

/// new_register/tests/sq1.mnc and sq2.mnc: the same 200 mm cube at 2 mm and
/// 4 mm, both starting at -100.
VolumeGeometry sq1Geometry()
{
    return makeGeometry(glm::ivec3(100), glm::dvec3(2.0), glm::dvec3(-100.0));
}

VolumeGeometry sq2Geometry()
{
    return makeGeometry(glm::ivec3(50), glm::dvec3(4.0), glm::dvec3(-100.0));
}

/// Cross-volume cursor synchronisation goes through world space, as
/// new_register's does. The same grid on both sides must be an exact
/// identity -- the same subject in two modalities, where "the same slice"
/// has to mean the same index, not one rounded off by a voxel.
void testMapVoxelThroughWorldIsIdentityForTheSameGrid()
{
    VolumeGeometry sq1 = sq1Geometry();
    for (int i = 0; i < 100; i += 7)
    {
        glm::ivec3 voxel(i, 99 - i, (i * 3) % 100);
        assert(mapVoxelThroughWorld(voxel, sq1, sq1) == voxel);
    }
}

/// Half the resolution over the same extent: voxel 48 of sq1 is world -4,
/// which is voxel 24 of sq2.
void testMapVoxelThroughWorldFollowsStep()
{
    VolumeGeometry sq1 = sq1Geometry();
    VolumeGeometry sq2 = sq2Geometry();
    assert(mapVoxelThroughWorld(glm::ivec3(48), sq1, sq2) == glm::ivec3(24));
    assert(mapVoxelThroughWorld(glm::ivec3(0), sq1, sq2) == glm::ivec3(0));
    assert(mapVoxelThroughWorld(glm::ivec3(24), sq2, sq1) == glm::ivec3(48));
}

/// A shifted origin shifts the index -- the case a proportional index
/// mapping gets wrong, since the two grids have the same dimensions.
void testMapVoxelThroughWorldFollowsStart()
{
    VolumeGeometry sq1 = sq1Geometry();
    VolumeGeometry shifted = makeGeometry(glm::ivec3(100), glm::dvec3(2.0),
                                          glm::dvec3(-90.0, -100.0, -110.0));
    // World (-4, -4, -4).
    assert(mapVoxelThroughWorld(glm::ivec3(48), sq1, shifted) == glm::ivec3(43, 48, 53));
}

/// Direction cosines are honoured: a volume whose first voxel axis runs
/// along world Y and second along world X swaps those two components.
void testMapVoxelThroughWorldFollowsDirectionCosines()
{
    VolumeGeometry plain = makeGeometry(glm::ivec3(10), glm::dvec3(1.0), glm::dvec3(0.0));
    glm::dmat3 swapped(0.0, 1.0, 0.0,
                       1.0, 0.0, 0.0,
                       0.0, 0.0, 1.0);
    VolumeGeometry permuted = makeGeometry(glm::ivec3(10), glm::dvec3(1.0), glm::dvec3(0.0),
                                           swapped);
    assert(mapVoxelThroughWorld(glm::ivec3(2, 7, 4), plain, permuted) == glm::ivec3(7, 2, 4));
}

/// Outside the target's field of view the index is reported as it falls,
/// not clamped: a column past the end of a shorter volume shows background
/// rather than repeating its edge slice, so the caller must see that it is
/// outside.
void testMapVoxelThroughWorldDoesNotClampOutsideTheTarget()
{
    VolumeGeometry sq1 = sq1Geometry();
    VolumeGeometry small = makeGeometry(glm::ivec3(10), glm::dvec3(2.0), glm::dvec3(0.0));
    // World (-100, 0, 98): voxel (-50, 0, 49) of `small`.
    assert(mapVoxelThroughWorld(glm::ivec3(0, 50, 99), sq1, small) == glm::ivec3(-50, 0, 49));
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
    testAxisForViewIndex();
    testSliceCountForView();
    testMapVoxelThroughWorldIsIdentityForTheSameGrid();
    testMapVoxelThroughWorldFollowsStep();
    testMapVoxelThroughWorldFollowsStart();
    testMapVoxelThroughWorldFollowsDirectionCosines();
    testMapVoxelThroughWorldDoesNotClampOutsideTheTarget();
    testAspectAxesTable();
    testAspectAxesAgainstThickSlicesVolume(path);

    return 0;
}
