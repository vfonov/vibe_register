#pragma once

#include <algorithm>
#include <cmath>

/// Conversions between a slice texture coordinate and a voxel index along
/// one texture axis. A slice texture has one texel per voxel: texel i covers
/// normalised coordinates [i/dim, (i+1)/dim).

/// Normalised texture coordinate -> index of the texel under it, clamped to 0..dim-1.
inline int uvToVoxel(float u, int dim)
{
    int i = static_cast<int>(std::floor(u * static_cast<float>(dim)));
    return std::clamp(i, 0, std::max(dim - 1, 0));
}

/// Voxel index -> normalised texture coordinate of its texel centre.
inline float voxelToUv(int i, int dim)
{
    return (static_cast<float>(i) + 0.5f) / static_cast<float>(std::max(dim, 1));
}
