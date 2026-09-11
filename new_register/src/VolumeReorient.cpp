#include "VolumeReorient.h"

#include <cstddef>
#include <stdexcept>

bool isIdentityReorient(const std::array<int, 3>& fileAxisToSpatial,
                        const std::array<bool, 3>& flip)
{
    return fileAxisToSpatial[0] == 0 && fileAxisToSpatial[1] == 1 && fileAxisToSpatial[2] == 2 &&
           !flip[0] && !flip[1] && !flip[2];
}

std::vector<float> reorientToStandard(const float* src,
                                      const std::array<int, 3>& fileDims,
                                      const std::array<int, 3>& fileAxisToSpatial,
                                      const std::array<bool, 3>& flip)
{
    std::array<bool, 3> seen{false, false, false};
    for (int j = 0; j < 3; ++j)
    {
        int d = fileAxisToSpatial[j];
        if (d < 0 || d > 2 || seen[d])
            throw std::invalid_argument("reorientToStandard: axis map is not a permutation");
        seen[d] = true;
    }

    // Length of each spatial axis, and its stride in data[z*ny*nx + y*nx + x].
    std::array<std::ptrdiff_t, 3> spatialDims{0, 0, 0};
    for (int j = 0; j < 3; ++j)
        spatialDims[fileAxisToSpatial[j]] = fileDims[j];
    const std::array<std::ptrdiff_t, 3> spatialStride{
        1, spatialDims[0], spatialDims[0] * spatialDims[1]};

    // Signed destination stride per file axis; a flipped axis starts at its far end.
    std::array<std::ptrdiff_t, 3> stride{0, 0, 0};
    std::ptrdiff_t base = 0;
    for (int j = 0; j < 3; ++j)
    {
        int d = fileAxisToSpatial[j];
        stride[j] = spatialStride[d];
        if (flip[d])
        {
            base += (spatialDims[d] - 1) * spatialStride[d];
            stride[j] = -stride[j];
        }
    }

    const size_t total = static_cast<size_t>(fileDims[0]) * fileDims[1] * fileDims[2];
    std::vector<float> out(total);
    const float* s = src;
    std::ptrdiff_t offK = base;
    for (int k = 0; k < fileDims[2]; ++k, offK += stride[2])
    {
        std::ptrdiff_t offJ = offK;
        for (int j = 0; j < fileDims[1]; ++j, offJ += stride[1])
        {
            std::ptrdiff_t offI = offJ;
            for (int i = 0; i < fileDims[0]; ++i, offI += stride[0])
                out[static_cast<size_t>(offI)] = *s++;
        }
    }
    return out;
}
