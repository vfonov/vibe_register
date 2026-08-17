#pragma once

#include <optional>

namespace mriv::term
{

/// Maps a --axis argument (x|y|z) to the parent's renderSlice() viewIndex
/// convention: 0=axial(Z), 1=sagittal(X), 2=coronal(Y). Returns
/// std::nullopt for anything else so the CLI layer can report a clean
/// error instead of silently defaulting.
std::optional<int> viewIndexForAxis(char axis);

/// The two in-plane Volume axes (axisU, axisV) to pass to
/// Volume::slicePixelAspect() for a given viewIndex. Not derivable from
/// viewIndex by a simple formula -- verified against
/// SliceRenderer.cpp::renderSlice().
struct AspectAxes
{
    int u;
    int v;
};
AspectAxes aspectAxesForView(int viewIndex);

} // namespace mriv::term
