#include "render/SlicePipeline.hpp"

#include <algorithm>
#include <cstdint>

#include "render/SliceGeometry.hpp"

#include "SliceRenderer.h"
#include "Volume.h"

namespace mriv::term
{

namespace
{

// The same opaque black composeGrid() fills its canvas with, so a slice
// outside the volume reads as empty space rather than as a dark image.
constexpr uint32_t kBackground = 0xFF000000u;

/// The request's zoom, with its focus voxel turned into a position in the
/// rendered slice's pixels: the middle of that voxel, and counted from the
/// top, since renderSlice() draws rows bottom-up.
ZoomView zoomFor(const SliceRequest& request, const RenderedSlice& slice)
{
    ZoomView zoom;
    zoom.factor = request.zoom;
    if (request.hasFocus)
    {
        zoom.centreX = request.focusU + 0.5;
        zoom.centreY = slice.height - 1 - request.focusV + 0.5;
    }
    return zoom;
}

} // namespace

ResampledImage renderSliceForDisplay(const Volume& vol, const SliceRequest& request)
{
    VolumeRenderParams params;
    params.valueMin        = request.valueMin;
    params.valueMax        = request.valueMax;
    params.colourMap       = request.colourMap;
    params.invertColourMap = request.invertColourMap;

    RenderedSlice slice = renderSlice(vol, params, request.viewIndex, request.sliceIndex);
    if (slice.width <= 0 || slice.height <= 0)
        return ResampledImage{};

    auto axes     = aspectAxesForView(request.viewIndex);
    double aspect = vol.slicePixelAspect(axes.u, axes.v);

    ResampledImage image = resampleToDisplay(slice, aspect, request.maxWidth, request.maxHeight,
                                             request.scale, zoomFor(request, slice));

    // renderSlice() clamps the index itself, which is what gives this image
    // the right size; a slice the volume does not have is then blanked, so
    // its column shows background rather than a repeated edge slice.
    int count = sliceCountForView(request.viewIndex, vol.dimensions);
    if (request.sliceIndex < 0 || request.sliceIndex >= count)
        std::fill(image.pixels.begin(), image.pixels.end(), kBackground);

    return image;
}

ResampledImage renderOverlayForDisplay(const std::vector<const Volume*>& volumes,
                                       const std::vector<VolumeRenderParams>& params,
                                       const SliceRequest& request)
{
    // renderOverlaySlice() dereferences every entry and indexes params in
    // step with them, so both are checked here rather than trusted.
    if (volumes.size() < 2 || params.size() != volumes.size())
        return ResampledImage{};
    for (const Volume* vol : volumes)
        if (!vol)
            return ResampledImage{};

    RenderedSlice slice =
        renderOverlaySlice(volumes, params, request.viewIndex, request.sliceIndex);
    if (slice.width <= 0 || slice.height <= 0)
        return ResampledImage{};

    // The blend is drawn in the first volume's grid, so that volume's voxel
    // shape is the one to correct for.
    auto axes     = aspectAxesForView(request.viewIndex);
    double aspect = volumes.front()->slicePixelAspect(axes.u, axes.v);

    return resampleToDisplay(slice, aspect, request.maxWidth, request.maxHeight, request.scale,
                             zoomFor(request, slice));
}

} // namespace mriv::term
