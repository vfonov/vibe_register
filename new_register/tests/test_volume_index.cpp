/// test_volume_index.cpp — voxel index arithmetic for very large volumes.

#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>

#include "Volume.h"

TEST_CASE("voxelIndex does not overflow for volumes over INT_MAX voxels", "[volume][index]")
{
    // 2048 x 2048 x 1024 = 2^32 voxels; the last index is 2^32 - 1.
    const glm::ivec3 dims(2048, 2048, 1024);
    CHECK(voxelIndex(dims, 0, 0, 0) == 0u);
    CHECK(voxelIndex(dims, 2047, 0, 0) == 2047u);
    CHECK(voxelIndex(dims, 0, 1, 0) == 2048u);
    CHECK(voxelIndex(dims, 0, 0, 600) == 600ull * 2048ull * 2048ull);
    CHECK(voxelIndex(dims, 2047, 2047, 1023) == 4294967295ull);
}
