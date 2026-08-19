#pragma once

#include "render/Resample.hpp"

namespace mriv::term
{

/// Where the shared interactive cursor sits within one rendered slice, in
/// the slice's own *native* (pre-resample) pixel coordinates -- i.e. voxel
/// indices along the view's two in-plane axes (ViewState::crosshairFor()),
/// and the native pixel size renderSlice() produced for those same axes
/// (RenderedSlice::width/height, before resampleToDisplay() fits it into
/// the terminal's pixel box). drawCrosshair() maps u/v through
/// mapNativeToDisplay() itself, so the caller never has to know the
/// display image's actual size ahead of time.
struct CrosshairMark
{
    int u;
    int v;
    int nativeW;
    int nativeH;
};

/// Blend a one-pixel-wide crosshair (a full-height column, a full-width
/// row, no gap at the intersection) into `image` at the position `mark`
/// maps to. Mirrors new_register's ImGui crosshair overlay
/// (Interface.cpp) -- same colour and translucency -- baked directly into
/// the pixel buffer instead of drawn on top of a texture, and with the
/// same bottom-up row flip renderSlice() itself uses (PLAN.md).
///
/// A no-op on a degenerate image or a degenerate native size (nativeW/H
/// <= 0); mapNativeToDisplay() clamps u/v on its own, so an out-of-range
/// coordinate cannot index outside the buffer.
void drawCrosshair(ResampledImage& image, const CrosshairMark& mark);

} // namespace mriv::term
