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

/// The part of a Volume's metadata the synchronised cursor needs: its grid
/// and the parent's precomputed voxel<->world matrices (Volume::voxelToWorld
/// and Volume::worldToVoxel), copied out so the interactive model stays free
/// of Volume and its IO.
struct VolumeGeometry
{
    glm::ivec3 dimensions{1, 1, 1};
    glm::dmat4 voxelToWorld{1.0};
    glm::dmat4 worldToVoxel{1.0};
};

/// Map a voxel from one volume onto another through world space, for the
/// synchronised cursor a multi-volume display shares: `voxel` in `from` goes
/// to world coordinates, then to the nearest voxel of `to`. The same steps
/// as Volume::transformWorldToVoxel(), except the result is *not* clamped
/// to `to`'s grid: a point outside `to`'s field of view comes back out of
/// range, so the caller can show background there instead of an edge
/// slice.
glm::ivec3 mapVoxelThroughWorld(const glm::ivec3& voxel,
                                const VolumeGeometry& from,
                                const VolumeGeometry& to);

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
