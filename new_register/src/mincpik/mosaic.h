/// mosaic.h — Mosaic layout and slice manipulation utilities for new_mincpik.

#ifndef MINCPIK_MOSAIC_H
#define MINCPIK_MOSAIC_H

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
/// Distributes N slices evenly across the volume extent, avoiding the
/// very first and last slices (which are typically blank).
std::vector<int> evenlySpacedSlices(const Volume& vol, int viewIndex, int count);

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
