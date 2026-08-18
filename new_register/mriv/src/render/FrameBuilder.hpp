#pragma once

#include <vector>

#include "render/Resample.hpp"

#include "ColourMap.h"

class Volume;

namespace mriv::term
{

/// One column of the display: a volume plus how it should be shown. Each
/// volume carries its own intensity range and colour map -- the point of
/// putting two volumes side by side is usually that they need different
/// ones.
struct FramePane
{
    const Volume* volume = nullptr;

    /// The slice to show for each entry of FrameRequest::views, in the same
    /// order. Supplied by the caller rather than derived here: the cursor
    /// lives in the first volume's index space and is mapped across volumes
    /// by mapSliceIndex() (render/SliceGeometry.hpp), and the status line
    /// needs the same numbers, so they are computed once by the caller.
    std::vector<int> sliceIndices;

    double valueMin = 0.0;
    double valueMax = 1.0;
    ColourMapType colourMap = ColourMapType::GrayScale;
    bool invertColourMap = false;
};

/// A gutter between panes, so two volumes side by side do not read as one
/// wide image.
constexpr int kDefaultGap = 4;

/// Everything needed to build one displayed frame.
struct FrameRequest
{
    /// Columns, in command-line order.
    std::vector<FramePane> panes;

    /// Rows, in --views order, as parent viewIndex values.
    std::vector<int> views;

    /// The terminal's pixel box for the image (the status row excluded).
    int boxWidth = 0;
    int boxHeight = 0;

    int gap = kDefaultGap;

    /// Integer pixel magnification, applied per pane after the fit.
    int scale = 1;
};

/// Render every (view, volume) cell and composite them into the single
/// RGBA buffer that gets blitted. Rows are views, columns are volumes.
///
/// The box is a budget, not the output size: each slice is fitted into its
/// share of it, and the frame is then sized to what those fits produced.
/// renderSliceForDisplay() never upscales, so sizing the frame to the box
/// would surround a small volume with a screen of black and blit megabytes
/// of it on every keypress.
///
/// Shared by the one-shot path and the interactive loop so the two cannot
/// drift: `mriv a.mnc b.mnc` piped to a file and the same command on a
/// terminal must show the same layout.
///
/// A cell with no volume, no slice index or an empty render is left blank
/// rather than failing the frame -- one bad column should not cost the
/// user the others. Returns an empty image for a degenerate request (no
/// panes, no views, or an empty box).
ResampledImage buildFrame(const FrameRequest& request);

} // namespace mriv::term
