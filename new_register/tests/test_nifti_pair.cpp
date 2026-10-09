/// test_nifti_pair.cpp — a two-file NIfTI (.hdr + .img) next to a single-file
/// .nii.gz of the same object.
///
/// Fixtures (tests/pair_t1.hdr + pair_t1.img, tests/pair_seg.nii.gz) are
/// written by generate_nifti_fixtures: a synthetic off-centre ellipsoid and
/// its mask on the same world grid, the pair stored with a negative x step.

#include <cmath>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "Volume.h"
#include "nr_test.h"

namespace
{

Volume loadFixture(const std::string& name)
{
    REQUIRE_FALSE(nrtest::srcDir().empty());
    Volume vol;
    vol.load(nrtest::srcDir() + "/" + name);
    return vol;
}

} // namespace

TEST_CASE("two-file NIfTI loads onto the same grid as its .nii.gz mask", "[nifti][pair]")
{
    const Volume t1 = loadFixture("pair_t1.hdr");
    const Volume seg = loadFixture("pair_seg.nii.gz");

    CHECK(t1.dimensions == glm::ivec3(24, 30, 20));
    CHECK(seg.dimensions == t1.dimensions);
    CHECK(t1.step == glm::dvec3(2.0));
    CHECK(t1.start == glm::dvec3(-24.0, -30.0, -20.0));
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
        {
            INFO("voxelToWorld[" << c << "][" << r << "]");
            CHECK(std::fabs(t1.voxelToWorld[c][r] - seg.voxelToWorld[c][r]) < 1e-6);
        }

    CHECK(t1.min_value == 0.0f);
    CHECK(t1.max_value > 900.0f);
    CHECK(seg.min_value == 0.0f);
    CHECK(seg.max_value == 1.0f);

    // The mask marks exactly the non-zero T1 voxels; the ellipsoid is
    // off-centre, so this fails if the pair's stored x flip is not undone.
    size_t inside = 0;
    for (int z = 0; z < t1.dimensions.z; ++z)
        for (int y = 0; y < t1.dimensions.y; ++y)
            for (int x = 0; x < t1.dimensions.x; ++x)
            {
                const bool masked = seg.get(x, y, z) > 0.5f;
                if ((t1.get(x, y, z) > 0.0f) != masked)
                    FAIL("mask and image disagree at " << x << "," << y << "," << z);
                inside += masked ? 1 : 0;
            }
    CHECK(inside > 1000);
}

TEST_CASE("two-file NIfTI loads the same from its .img name", "[nifti][pair]")
{
    const Volume viaHdr = loadFixture("pair_t1.hdr");
    const Volume viaImg = loadFixture("pair_t1.img");
    CHECK(viaImg.dimensions == viaHdr.dimensions);
    CHECK(viaImg.voxelToWorld == viaHdr.voxelToWorld);
    CHECK(viaImg.data == viaHdr.data);
}
