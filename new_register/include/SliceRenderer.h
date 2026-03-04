#pragma once

#include <vector>
#include <cstdint>
#include <unordered_map>

#include <glm/glm.hpp>

#include "Volume.h"
#include "ColourMap.h"
#include "Transform.h"

/// Constants for under/over colour clamping modes.
constexpr int kSliceClampCurrent     = -2;
constexpr int kSliceClampTransparent = -1;

/// Per-volume rendering parameters (headless equivalent of VolumeViewState).
struct VolumeRenderParams
{
    double valueMin = 0.0;
    double valueMax = 1.0;
    ColourMapType colourMap = ColourMapType::GrayScale;
    float overlayAlpha = 1.0f;
    int underColourMode = kSliceClampCurrent;
    int overColourMode  = kSliceClampCurrent;
};

/// Result of rendering a single slice — a CPU pixel buffer.
struct RenderedSlice
{
    std::vector<uint32_t> pixels;   ///< packed 0xAABBGGRR (little-endian RGBA)
    int width  = 0;
    int height = 0;
};

/// Render a single 2D slice from one volume.
/// @param vol        The volume to slice.
/// @param params     Colour map, value range, clamping.
/// @param viewIndex  0=axial(Z), 1=sagittal(X), 2=coronal(Y).
/// @param sliceIndex The slice position along the slicing axis.
/// @return A RenderedSlice with RGBA pixel data.
RenderedSlice renderSlice(
    const Volume& vol,
    const VolumeRenderParams& params,
    int viewIndex,
    int sliceIndex);

/// Render an overlay composite of multiple volumes at a given plane position.
///
/// All volumes are resampled into volume 0's voxel grid and alpha-blended.
/// The algorithm matches ViewManager::updateOverlayTexture() exactly.
///
/// @param volumes       Pointers to loaded volumes (volume 0 is the reference).
/// @param params        Per-volume rendering parameters.
/// @param viewIndex     0=axial(Z), 1=sagittal(X), 2=coronal(Y).
/// @param sliceIndices  Per-volume slice index for this view (index into the
///                      slicing axis of each volume).  Only element 0 is used
///                      for the output grid; the remaining are ignored since
///                      overlay always samples using volume 0's geometry.
/// @param transform     Optional registration transform (vol 0 -> vol 1).
/// @return A RenderedSlice with RGBA pixel data.
RenderedSlice renderOverlaySlice(
    const std::vector<const Volume*>& volumes,
    const std::vector<VolumeRenderParams>& params,
    int viewIndex,
    int sliceIndex,
    const TransformResult* transform = nullptr);
