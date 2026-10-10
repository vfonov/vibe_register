#pragma once

#include <vector>

#include "render/Resample.hpp"

#include "ColourMap.h"
#include "SliceRenderer.h"

class Volume;

namespace mriv::term
{

/// Everything needed to turn a loaded Volume into one image ready for the
/// terminal. Shared by the one-shot CLI path and the interactive loop --
/// both need the same slice render and aspect-correct resample, and a
/// private copy in either would drift.
struct SliceRequest
{
    /// 0=axial(Z), 1=sagittal(X), 2=coronal(Y), per the parent's
    /// renderSlice() convention. See render/SliceGeometry.hpp.
    int viewIndex = 0;
    int sliceIndex = 0;

    /// The parent's intensity range model: valueMin maps to the darkest
    /// colour, valueMax to the brightest.
    double valueMin = 0.0;
    double valueMax = 1.0;

    ColourMapType colourMap = ColourMapType::GrayScale;
    bool invertColourMap = false;

    /// The display box in pixels. The image is fitted inside it with the
    /// aspect correction applied, never upscaled beyond native size.
    int maxWidth = 0;
    int maxHeight = 0;

    /// Integer pixel magnification applied after the fit (default 1).
    int scale = 1;

    /// Zoom factor (render/Resample.hpp, ZoomView): 1 shows the whole
    /// slice at its fitted size.
    double zoom = 1.0;

    /// The voxel a zoomed view is centred on, along the view's two in-plane
    /// axes -- the same coordinates as a crosshair. Without one the view is
    /// centred on the middle of the slice.
    bool hasFocus = false;
    int focusU = 0;
    int focusV = 0;
};

/// resampleToDisplay() for a RenderedSlice from the parent's renderSlice().
/// Lives here rather than in render/Resample.hpp so that Resample stays free
/// of nr_core (it is part of mriv_term).
inline ResampledImage resampleToDisplay(
    const RenderedSlice& slice, double aspect, int maxW, int maxH, int scale = 1,
    const ZoomView& zoom = ZoomView{})
{
    return resampleToDisplay(slice.pixels.data(), slice.width, slice.height,
                             aspect, maxW, maxH, scale, zoom);
}

/// Render one slice and resample it to the display box, applying the
/// Volume::slicePixelAspect() correction for the view's in-plane axes.
/// Returns an empty image (width and height 0) when the slice is empty,
/// which is what the parent's renderSlice() gives for an empty volume
/// (mriv/HANDOFF.md sec 3.7). A sliceIndex outside the volume gives an
/// opaque-black image of the size an in-range slice would have -- a column
/// whose volume does not reach the shared cursor.
ResampledImage renderSliceForDisplay(const Volume& vol, const SliceRequest& request);

/// Blend several volumes into one image ready for the terminal: the
/// parent's renderOverlaySlice(), which resamples every volume into the
/// first one's grid, followed by the same aspect-correct resample
/// renderSliceForDisplay() does. `params` is parallel to `volumes` and
/// carries each one's range and colour map; of `request` only the view, the
/// slice index (in the first volume), the display box, the scale and the
/// zoom with its focus (a voxel of the first volume) are used.
///
/// The image has the size the first volume's slice would have. A slice
/// index outside the first volume is not blanked: the other volumes are
/// still drawn wherever they reach, which is what navigation over the
/// union of the volumes needs. Returns an empty image for fewer than two
/// volumes, a null entry, or mismatched `params`.
ResampledImage renderOverlayForDisplay(const std::vector<const Volume*>& volumes,
                                       const std::vector<VolumeRenderParams>& params,
                                       const SliceRequest& request);

} // namespace mriv::term
