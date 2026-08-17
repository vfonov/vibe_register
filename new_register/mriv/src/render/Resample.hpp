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
ResampledImage resampleToDisplay(const RenderedSlice& slice, double aspect, int maxW, int maxH);

} // namespace mriv::term
