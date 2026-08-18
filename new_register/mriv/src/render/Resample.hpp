#pragma once

#include <cstdint>
#include <vector>

#include "SliceRenderer.h"

namespace mriv::term
{

struct ResampleSize
{
    int width;
    int height;
};

/// Compute the display size for a slice of native size (w,h) whose voxels
/// have physical aspect ratio `aspect` (Volume::slicePixelAspect(u,v)),
/// scaled to fit within (maxW, maxH) while preserving the corrected
/// aspect ratio. Never returns a zero dimension.
ResampleSize computeResampleSize(int w, int h, double aspect, int maxW, int maxH);

/// Nearest-neighbour resample of a packed RGBA (0xAABBGGRR) buffer of
/// size (w,h) to (outW,outH).
std::vector<uint32_t> resamplePixelsNearest(
    const uint32_t* src, int w, int h, int outW, int outH);

struct ResampledImage
{
    std::vector<uint32_t> pixels;
    int width  = 0;
    int height = 0;
};

/// Convenience wrapper combining computeResampleSize() and
/// resamplePixelsNearest() for a RenderedSlice from the parent's
/// renderSlice().
///
/// `scale` is an integer pixel-magnification factor (default 1, no
/// magnification): the slice is first fit into (maxW/scale, maxH/scale)
/// as usual, then each resulting pixel is replicated into a scale x scale
/// block via nearest-neighbour resampling, so the final image never
/// exceeds (maxW, maxH) by more than (scale - 1) pixels in either
/// dimension. Values <= 1 are treated as no magnification.
ResampledImage resampleToDisplay(
    const RenderedSlice& slice, double aspect, int maxW, int maxH, int scale = 1);

} // namespace mriv::term
