#include "render/SlicePipeline.hpp"

#include "render/SliceGeometry.hpp"

#include "SliceRenderer.h"
#include "Volume.h"

namespace mriv::term
{

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

    return resampleToDisplay(slice, aspect, request.maxWidth, request.maxHeight, request.scale);
}

} // namespace mriv::term
