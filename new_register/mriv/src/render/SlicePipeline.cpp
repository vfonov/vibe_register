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

    ResampledImage image =
        resampleToDisplay(slice, aspect, request.maxWidth, request.maxHeight, request.scale);

    // renderSlice() clamps the index itself, which is what gives this image
    // the right size; a slice the volume does not have is then blanked, so
    // its column shows background rather than a repeated edge slice.
    int count = sliceCountForView(request.viewIndex, vol.dimensions);
    if (request.sliceIndex < 0 || request.sliceIndex >= count)
        std::fill(image.pixels.begin(), image.pixels.end(), kBackground);

    return image;
}

} // namespace mriv::term
