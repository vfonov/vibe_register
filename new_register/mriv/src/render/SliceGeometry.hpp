#pragma once

#include <optional>

#include <glm/glm.hpp>

namespace mriv::term
{

/// Maps a --axis argument (x|y|z) to the parent's renderSlice() viewIndex
/// convention: 0=axial(Z), 1=sagittal(X), 2=coronal(Y). Returns
/// std::nullopt for anything else so the CLI layer can report a clean
/// error instead of silently defaulting.
std::optional<int> viewIndexForAxis(char axis);

/// The inverse of viewIndexForAxis(). Anything outside [0,2] falls back to
/// 'z', matching the other tables' treatment of an out-of-range viewIndex.
char axisForViewIndex(int viewIndex);

/// The number of slices available along the axis a given view slices
/// through: viewIndex 0 (axial) walks Z, 1 (sagittal) walks X, 2
/// (coronal) walks Y. Anything else falls back to Z, matching
/// aspectAxesForView()'s treatment of an out-of-range viewIndex.
int sliceCountForView(int viewIndex, const glm::ivec3& dimensions);

/// Map a slice index from one volume's axis to another's, for the
/// synchronised cursor a multi-volume display shares. `index` is a position
/// in an axis of `fromCount` slices; the result is the proportionally
/// equivalent position in an axis of `toCount`, clamped to a valid index.
///
/// Equal counts give an exact identity, which is what makes the common case
/// -- the same subject in two modalities -- behave the way a user expects:
/// "the same slice" means the same index, not one rounded off by a pixel.
/// A count of zero yields 0 rather than dividing by it.
int mapSliceIndex(int index, int fromCount, int toCount);

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
