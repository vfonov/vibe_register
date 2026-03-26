/// mosaic.h — Mosaic layout and slice manipulation utilities for new_mincpik.

#ifndef MINCPIK_MOSAIC_H
#define MINCPIK_MOSAIC_H

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "SliceRenderer.h"
#include "Volume.h"

/// Parse a comma-separated list of doubles.
std::vector<double> parseDoubleList(const std::string& str);

/// Parse a comma-separated list of floats.
std::vector<float> parseFloatList(const std::string& str);

/// Convert world coordinate to voxel index along a given axis.
/// viewIndex: 0=axial(Z), 1=sagittal(X), 2=coronal(Y).
int worldToSliceVoxel(const Volume& vol, int viewIndex, double worldCoord);

/// Compute evenly spaced voxel indices for a given number of slices.
/// cropLo / cropHi: voxels to exclude from the low/high end of the axis
/// (defaults 0 = no crop).  When non-zero, overrides the default 10% margins.
std::vector<int> evenlySpacedSlices(const Volume& vol, int viewIndex, int count,
                                    int cropLo = 0, int cropHi = 0);

/// Crop a rendered slice to the region specified by crop=[x1,x2,y1,y2,z1,z2].
/// sliceIndex is the voxel index along the cutting axis.
/// If the slice position is outside the cropped range, returns a blank
/// (opaque-black) tile of the cropped dimensions.
/// If crop is empty (nullopt), returns the slice unchanged.
RenderedSlice applyCrop(const RenderedSlice& slice,
                        const Volume& vol,
                        int viewIndex,
                        int sliceIndex,
                        const std::array<int,6>& crop);

/// Determine which two volume axes correspond to the in-plane (U, V)
/// directions for a given view.
///   viewIndex 0 (axial):    U=X(0), V=Y(1)
///   viewIndex 1 (sagittal): U=Y(1), V=Z(2)
///   viewIndex 2 (coronal):  U=X(0), V=Z(2)
void viewAxes(int viewIndex, int& axisU, int& axisV);

/// Resample a rendered slice so that its pixel dimensions reflect the
/// physical (world-space) voxel spacing at the finest resolution present
/// in the volume.
RenderedSlice resampleToPhysicalAspect(
    const RenderedSlice& slice,
    const Volume& vol,
    int viewIndex);

/// Blit a RenderedSlice into a larger pixel buffer at (destX, destY).
void blitSlice(
    const RenderedSlice& slice,
    std::vector<uint32_t>& dest,
    int destWidth,
    int destX,
    int destY);

#endif // MINCPIK_MOSAIC_H
