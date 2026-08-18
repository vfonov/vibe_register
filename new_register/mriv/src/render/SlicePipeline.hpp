#pragma once

#include "render/Resample.hpp"

#include "ColourMap.h"

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
};

/// Render one slice and resample it to the display box, applying the
/// Volume::slicePixelAspect() correction for the view's in-plane axes.
/// Returns an empty image (width and height 0) when the slice is empty,
/// which is what the parent's renderSlice() gives for an empty volume
/// (mriv/HANDOFF.md sec 3.7).
ResampledImage renderSliceForDisplay(const Volume& vol, const SliceRequest& request);

} // namespace mriv::term
