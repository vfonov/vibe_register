#pragma once

#include <vector>

#include "render/Layout.hpp"
#include "render/Resample.hpp"

namespace mriv::term
{

/// One cell's rendered image and the rectangle it belongs in. `image` may
/// be null, and may be empty: a cell whose render failed leaves its area
/// blank rather than costing the whole frame.
struct PlacedImage
{
    const ResampledImage* image = nullptr;
    CellRect rect;
};

/// Composite the cells into a single RGBA (0xAABBGGRR) buffer of the given
/// size. Each image is centred in its rectangle and clipped to it.
///
/// The whole grid travels to the terminal as one bitmap. That keeps the
/// notcurses wrappers free of per-pane plane bookkeeping, makes the layout
/// testable with no terminal at all, and makes redrawing a frame -- or
/// re-blitting the final one after interactive mode exits -- a matter of
/// handing over a buffer that already exists.
///
/// Uncovered area is opaque black, never transparent: a terminal blitting
/// an alpha-zero pixel shows whatever was underneath it, which on a redraw
/// is the previous frame.
ResampledImage composeGrid(const std::vector<PlacedImage>& cells, int width, int height);

} // namespace mriv::term
