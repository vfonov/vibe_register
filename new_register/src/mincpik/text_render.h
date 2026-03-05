/// text_render.h — Text rendering utilities for new_mincpik.

#ifndef MINCPIK_TEXT_RENDER_H
#define MINCPIK_TEXT_RENDER_H

#include <cstdint>
#include <string>

#include "SliceRenderer.h"

/// Parse a foreground colour from a string.
/// Supports:
///   - Hex: #RRGGBB, #RGB, RRGGBB, RGB (with or without '#' prefix)
///   - Named colours: white, black, red, green, blue, yellow, cyan, magenta,
///                    gray/grey, orange
/// Returns packed 0xAABBGGRR (little-endian RGBA) matching the mosaic format.
/// Returns 0xFFFFFFFF (white) on parse failure.
uint32_t parseFgColour(const std::string& str);

/// Render a line of text into a RenderedSlice using stb_easy_font.
///
/// @param text       The ASCII text to render.
/// @param fgColour   Packed 0xAABBGGRR foreground colour.
/// @param scale      Integer scale factor (1 = native 12px height).
/// @return A RenderedSlice containing the rasterized text on a transparent
///         background (alpha = 0 where no glyph).
RenderedSlice renderTextRow(
    const std::string& text,
    uint32_t fgColour,
    int scale);

#endif // MINCPIK_TEXT_RENDER_H
