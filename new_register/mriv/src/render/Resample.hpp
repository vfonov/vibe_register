#pragma once

#include <cstdint>
#include <vector>

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

/// The part of a source image a resampled image shows, in source pixels
/// (fractional: a zoomed view rarely starts on a pixel edge). A zero size
/// means "not recorded", which readers treat as the whole source.
struct SourceWindow
{
    double x = 0.0;
    double y = 0.0;
    double width  = 0.0;
    double height = 0.0;
};

struct ResampledImage
{
    std::vector<uint32_t> pixels;
    int width  = 0;
    int height = 0;

    /// What resampleToDisplay() showed of its source: all of it unless a
    /// zoom cropped it. drawCrosshair() needs this to find the cursor in a
    /// zoomed pane.
    SourceWindow window;
};

/// A zoom for resampleToDisplay(): how much larger than the plain fit the
/// source is drawn, and the source pixel to keep in view when that no
/// longer fits the box. A negative centre means the middle of the source.
struct ZoomView
{
    double factor  = 1.0;
    double centreX = -1.0;
    double centreY = -1.0;
};

/// Convenience wrapper combining computeResampleSize() and
/// resamplePixelsNearest() for a packed RGBA (0xAABBGGRR) buffer of size
/// (w,h) -- a slice from the parent's renderSlice() (see the RenderedSlice
/// overload in render/SlicePipeline.hpp) or a decoded 2D image (mriv_qc).
/// An empty source (w or h <= 0) gives an empty image.
///
/// `scale` is an integer pixel-magnification factor (default 1, no
/// magnification): the image is first fit into (maxW/scale, maxH/scale)
/// as usual, then each resulting pixel is replicated into a scale x scale
/// block via nearest-neighbour resampling, so the final image never
/// exceeds (maxW, maxH) by more than (scale - 1) pixels in either
/// dimension. Values <= 1 are treated as no magnification.
///
/// `zoom` draws the source `zoom.factor` times larger than that fit (or
/// smaller, below 1). The image grows with it until it fills the box; past
/// that the box stays full and the source is cropped to a window around
/// (zoom.centreX, zoom.centreY), slid back inside the source where the
/// centre is near an edge. Unlike the plain fit this does enlarge a source
/// beyond its native size -- that is what zooming in on a small volume
/// means. A factor of exactly 1 is the plain fit, pixel for pixel. The
/// result's `window` records what was shown.
ResampledImage resampleToDisplay(
    const uint32_t* src, int w, int h, double aspect, int maxW, int maxH, int scale = 1,
    const ZoomView& zoom = ZoomView{});

/// Where a source coordinate along one axis lands in an image `displaySize`
/// pixels long that shows `windowSize` source pixels starting at
/// `windowStart` -- mapNativeToDisplay() for a cropped view. `sourceCoord`
/// is a pixel index; its centre is what is mapped. Returns -1 when that
/// pixel is not in the window, or a size is not positive.
int mapSourceToWindow(int sourceCoord, double windowStart, double windowSize, int displaySize);

/// Where a native (pre-resample) coordinate along one axis lands after
/// resamplePixelsNearest() scales `nativeSize` pixels to `displaySize`
/// pixels -- the centre-of-source-pixel convention resamplePixelsNearest()
/// uses to pick a source pixel, inverted to pick a destination pixel.
/// Clamped to the last display pixel; a non-positive size returns 0.
int mapNativeToDisplay(int nativeCoord, int nativeSize, int displaySize);

} // namespace mriv::term
