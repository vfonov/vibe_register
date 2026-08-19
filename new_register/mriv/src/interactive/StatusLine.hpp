#pragma once

#include <string>
#include <vector>

#include "interactive/ViewState.hpp"

namespace mriv::term
{

/// One line per loaded volume, in column order, plain file names with no
/// path and no marker -- which one is active is shown by the '*' Overlay
/// draws beneath its column, not by text here. Printed above the status
/// line, one row per volume, so the grid's columns can be told apart before
/// the image is even on screen.
std::vector<std::string> formatVolumeNameLines(const std::vector<std::string>& paths);

/// The single reserved row below the volume names: where the cursor is,
/// how the active volume is mapped, and which keys do what.
///
/// The key legend is not decoration. Interactive mode has no help panel, so
/// this line is the only thing that makes the bindings discoverable. The
/// result never contains a newline -- it occupies exactly one terminal row,
/// and wrapping it would push the image down and corrupt the layout.
///
/// `paths` is only used to name the active volume in the range prompt;
/// which file that is otherwise comes from the '*' beneath its column, not
/// from this line.
std::string formatStatusLine(const ViewState& state, const std::vector<std::string>& paths);

/// The same line without the key legend: where the cursor is and how the
/// active volume is mapped, and nothing about keys.
///
/// Printed above the frame that is left on the terminal after interactive
/// mode exits. The keys are gone by then, but what the picture shows still
/// needs saying.
std::string formatSummaryLine(const ViewState& state, const std::vector<std::string>& paths);

/// The caption printed above a multi-volume one-shot render. That path
/// blits the whole grid as one image, so a caption naming the columns in
/// order is the only way to tell them apart.
std::string formatCaption(const std::vector<std::string>& paths);

} // namespace mriv::term
