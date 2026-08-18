#pragma once

#include <string>

#include "interactive/ViewState.hpp"

namespace mriv::term
{

/// The single reserved row above the image in interactive mode: where we
/// are, and which keys do what.
///
/// The key legend is not decoration. Interactive mode has no help panel, so
/// this line is the only thing that makes the bindings discoverable. The
/// result never contains a newline -- it occupies exactly one terminal row,
/// and wrapping it would push the image down and corrupt the layout.
std::string formatStatusLine(const ViewState& state, const std::string& path);

} // namespace mriv::term
