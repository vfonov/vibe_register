#pragma once

#include <array>
#include <vector>

/// True when the file buffer is already in Volume::data layout
/// (file axis j holds spatial axis j and no axis is flipped).
bool isIdentityReorient(const std::array<int, 3>& fileAxisToSpatial,
                        const std::array<bool, 3>& flip);

/// Reorder a 3-D voxel buffer from file storage order into Volume::data layout
/// (index = z*ny*nx + y*nx + x, every spatial axis ascending).
///   src                  fileDims[0]*fileDims[1]*fileDims[2] voxels, file axis 0 fastest-varying
///   fileAxisToSpatial[j] spatial axis (0=x, 1=y, 2=z) stored along file axis j
///   flip[d]              reverse spatial axis d (its file step was negative)
/// Throws std::invalid_argument if fileAxisToSpatial is not a permutation of {0,1,2}.
std::vector<float> reorientToStandard(const float* src,
                                      const std::array<int, 3>& fileDims,
                                      const std::array<int, 3>& fileAxisToSpatial,
                                      const std::array<bool, 3>& flip);
