#pragma once

#include <string>
#include <vector>

#include "render/FrameBuilder.hpp"

namespace mriv::term
{

/// A string to be written at a terminal cell position.
struct TextCell
{
    int row = 0;
    int col = 0;
    std::string text;
};

/// Where the image goes and what text goes around it, all in terminal
/// cells. Screen draws this literally: every entry of `text` at its own
/// position, then the bitmap at (imageRow, imageColumn).
struct FrameOverlay
{
    std::vector<TextCell> text;
    int imageRow = 0;
    int imageColumn = 0;
};

/// What both markers are drawn with.
constexpr char kMarker = '*';

/// Cells reserved to the left of the image for the active-view marker, and
/// rows reserved below it for the active-volume marker. Callers must take
/// these out of the pixel box before fitting the frame, or the marker row
/// falls off the bottom of the screen.
constexpr int kMarkerColumns = 2;
constexpr int kMarkerRows = 1;

/// Lay out the text that surrounds one frame.
///
/// `header` is drawn one line per row from the top -- the loaded volumes,
/// then the status row, then the hotkey row. `tracks` is the pane geometry
/// buildFrame() reported for the image, `activeRow` indexes its rows (the
/// view j/k moves) and `activeColumn` its columns (the volume c and r
/// change).
/// `cellWidth` and `cellHeight` are the terminal's pixels per cell, which
/// is what converts pane pixels into marker positions.
///
/// The markers are centred on their pane rather than aligned to its top or
/// left edge, so a '*' next to a tall row is not mistaken for the row
/// above it. A pane the tracks do not describe, or a terminal that reports
/// no cell size, is marked with nothing at all: a guessed position is
/// worse than none, since it would confidently point at the wrong volume.
FrameOverlay planOverlay(const std::vector<std::string>& header,
                         const FrameTracks& tracks,
                         int activeRow,
                         int activeColumn,
                         int imageHeight,
                         int cellWidth,
                         int cellHeight);

} // namespace mriv::term
