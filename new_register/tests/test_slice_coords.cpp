/// test_slice_coords.cpp — slice texture coordinate <-> voxel index.
///
/// A slice texture has one texel per voxel: texel i covers [i/dim, (i+1)/dim).
/// Clicking anywhere inside a texel must select that voxel, and the cursor
/// for voxel i is drawn at the texel centre (i + 0.5) / dim.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "SliceCoords.h"

using Catch::Matchers::WithinAbs;

TEST_CASE("A texture coordinate selects the texel under it", "[slicecoords]")
{
    const int dim = 4;  // texels [0,.25) [.25,.5) [.5,.75) [.75,1]
    CHECK(uvToVoxel(0.0f, dim) == 0);
    CHECK(uvToVoxel(0.20f, dim) == 0);
    CHECK(uvToVoxel(0.26f, dim) == 1);
    CHECK(uvToVoxel(0.49f, dim) == 1);
    CHECK(uvToVoxel(0.74f, dim) == 2);
    CHECK(uvToVoxel(0.80f, dim) == 3);
    CHECK(uvToVoxel(1.0f, dim) == 3);  // right edge stays in range
}

TEST_CASE("Out-of-range coordinates are clamped", "[slicecoords]")
{
    CHECK(uvToVoxel(-0.5f, 10) == 0);
    CHECK(uvToVoxel(1.5f, 10) == 9);
    CHECK(uvToVoxel(0.5f, 1) == 0);
}

TEST_CASE("The cursor is drawn at the texel centre", "[slicecoords]")
{
    CHECK_THAT(voxelToUv(0, 4), WithinAbs(0.125, 1e-6));
    CHECK_THAT(voxelToUv(3, 4), WithinAbs(0.875, 1e-6));
    CHECK_THAT(voxelToUv(0, 1), WithinAbs(0.5, 1e-6));
}

TEST_CASE("voxelToUv and uvToVoxel round-trip", "[slicecoords]")
{
    const int dim = GENERATE(1, 2, 3, 7, 193, 256);
    for (int i = 0; i < dim; ++i)
        REQUIRE(uvToVoxel(voxelToUv(i, dim), dim) == i);
}
