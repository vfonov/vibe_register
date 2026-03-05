/// colour_bar.h — Colour bar / legend rendering for new_mincpik.

#ifndef MINCPIK_COLOUR_BAR_H
#define MINCPIK_COLOUR_BAR_H

#include <cstdint>
#include <string>
#include <unordered_map>

#include "ColourMap.h"
#include "SliceRenderer.h"
#include "Volume.h"

/// Render a continuous colour bar showing a gradient from the given LUT
/// with min, midpoint, and max value labels.
///
/// @param lut        The colour lookup table to visualize.
/// @param valueMin   Lower bound of the displayed value range.
/// @param valueMax   Upper bound of the displayed value range.
/// @param extent     Length of the gradient strip along its primary axis (px).
/// @param fgColour   Text colour (packed 0xAABBGGRR).
/// @param fontScale  Integer scale factor for 12px base font.
/// @param horizontal If true, gradient runs left-to-right (for bottom bar);
///                   if false, gradient runs top-to-bottom (for right bar).
/// @return A RenderedSlice containing the bar on a transparent background.
RenderedSlice renderContinuousBar(
    const ColourLut& lut,
    double valueMin, double valueMax,
    int extent,
    uint32_t fgColour, int fontScale,
    bool horizontal);

/// Render a discrete label legend showing colour swatches and label names.
///
/// @param labelLUT   Map of label ID -> LabelInfo (colour + name).
/// @param fgColour   Text colour (packed 0xAABBGGRR).
/// @param fontScale  Integer scale factor for 12px base font.
/// @param maxWidth   Maximum width budget in pixels.
/// @param maxHeight  Maximum height budget in pixels.
/// @param horizontal If true, flow entries left-to-right with wrapping (bottom);
///                   if false, stack entries vertically (right).
/// @return A RenderedSlice containing the legend on a transparent background.
RenderedSlice renderLabelBar(
    const std::unordered_map<int, LabelInfo>& labelLUT,
    uint32_t fgColour, int fontScale,
    int maxWidth, int maxHeight,
    bool horizontal);

#endif // MINCPIK_COLOUR_BAR_H
