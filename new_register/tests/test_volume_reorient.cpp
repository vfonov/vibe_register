/// test_volume_reorient.cpp — VolumeReorient helper correctness (no data files needed).
///
/// Tests:
///   testAllPermutationsAndFlips — reorientToStandard matches a naive per-voxel reference
///     over every axis permutation and every flip combination.
///   testIdentityDetection       — isIdentityReorient is true only for the no-op case.
///   testBadAxisMapThrows        — a non-permutation axis map throws std::invalid_argument.

#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "VolumeReorient.h"

namespace
{

/// Naive reference: for every file index (i, j, k), place src[k*n1*n0 + j*n0 + i]
/// at the spatial location given by mapping file axes to spatial axes and flipping.
/// Same mapping as NiftiVolume.cpp's Stage 6 loop.
std::vector<float> naiveReorient(const float* src,
                                 const std::array<int, 3>& fileDims,
                                 const std::array<int, 3>& map,
                                 const std::array<bool, 3>& flip)
{
    const int n0 = fileDims[0], n1 = fileDims[1], n2 = fileDims[2];
    std::array<int, 3> n{0, 0, 0};
    for (int j = 0; j < 3; ++j)
        n[map[j]] = fileDims[j];
    const int nx = n[0], ny = n[1];

    std::vector<float> out(static_cast<size_t>(n0) * n1 * n2);
    for (int k = 0; k < n2; ++k)
    {
        for (int j = 0; j < n1; ++j)
        {
            for (int i = 0; i < n0; ++i)
            {
                int c[3];
                c[map[0]] = i;
                c[map[1]] = j;
                c[map[2]] = k;
                for (int d = 0; d < 3; ++d)
                    if (flip[d])
                        c[d] = n[d] - 1 - c[d];
                size_t dstIdx = static_cast<size_t>(c[2]) * ny * nx +
                                static_cast<size_t>(c[1]) * nx + c[0];
                size_t srcIdx = static_cast<size_t>(k) * n1 * n0 +
                                static_cast<size_t>(j) * n0 + i;
                out[dstIdx] = src[srcIdx];
            }
        }
    }
    return out;
}

void testAllPermutationsAndFlips()
{
    const std::array<int, 3> fileDims{2, 3, 5};  // non-cubic, all different
    const size_t total = static_cast<size_t>(fileDims[0]) * fileDims[1] * fileDims[2];
    std::vector<float> src(total);
    for (size_t i = 0; i < total; ++i)
        src[i] = static_cast<float>(i);

    std::array<int, 3> map{0, 1, 2};
    do
    {
        for (int mask = 0; mask < 8; ++mask)
        {
            std::array<bool, 3> flip{
                (mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0};

            std::vector<float> expected = naiveReorient(src.data(), fileDims, map, flip);
            std::vector<float> actual = reorientToStandard(src.data(), fileDims, map, flip);
            assert(actual == expected);

            bool expectIdentity =
                (map[0] == 0 && map[1] == 1 && map[2] == 2 && !flip[0] && !flip[1] && !flip[2]);
            assert(isIdentityReorient(map, flip) == expectIdentity);
            if (expectIdentity)
                assert(actual == src);
        }
    } while (std::next_permutation(map.begin(), map.end()));
}

void testBadAxisMapThrows()
{
    const std::array<int, 3> fileDims{2, 2, 2};
    std::vector<float> src(8, 0.0f);
    const std::array<int, 3> badMap{0, 0, 2};  // not a permutation (repeats 0, missing 1)
    const std::array<bool, 3> noFlip{false, false, false};

    bool threw = false;
    try
    {
        reorientToStandard(src.data(), fileDims, badMap, noFlip);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    assert(threw);
}

}  // namespace

int main()
{
    testAllPermutationsAndFlips();
    testBadAxisMapThrows();

    std::cout << "All VolumeReorient tests passed\n";
    return 0;
}
