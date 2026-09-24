#pragma once

#include <string>
#include <vector>

// What Screen::drawFrame() draws around the bitmap. Split out of
// interactive/Overlay.hpp, whose marker planning needs the render pipeline
// (and so nr_core), so that Screen and mriv_term do not.

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

} // namespace mriv::term
