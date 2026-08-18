#pragma once

#include <string>
#include <vector>

#include "interactive/ViewState.hpp"

namespace mriv::term
{

/// The single reserved row above the image in interactive mode: which
/// columns are loaded and which one is active, where the cursor is, how the
/// active volume is mapped, and which keys do what.
///
/// The key legend is not decoration. Interactive mode has no help panel, so
/// this line is the only thing that makes the bindings discoverable. The
/// result never contains a newline -- it occupies exactly one terminal row,
/// and wrapping it would push the image down and corrupt the layout.
///
/// `paths` is parallel to the state's volumes; only the file names are
/// shown, since a full path would push the legend off the row.
std::string formatStatusLine(const ViewState& state, const std::vector<std::string>& paths);

/// The caption printed above a multi-volume one-shot render. That path
/// blits the whole grid as one image, so a caption naming the columns in
/// order is the only way to tell them apart.
std::string formatCaption(const std::vector<std::string>& paths);

} // namespace mriv::term
