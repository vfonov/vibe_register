#pragma once

#include <string>

#include "qc/QcState.hpp"

namespace mriv::qc
{

// The three text rows above the image. Like mriv's StatusLine, none of them
// ever contains a newline or other control character: each occupies exactly
// one terminal row, and a stray '\n' from a quoted CSV field would push the
// image down and corrupt the layout.

/// "12/340  sub012  baseline  [Warning]  rated 57/340 (P40 W10 F7)". A '-'
/// visit (stdin / positional input) is left out; an unrated image shows
/// "[--]".
std::string formatHeader(const QcState& state);

/// The state's message if it has one, else the picture path -- prefixed
/// "cannot load" when `loadFailed`.
std::string formatDetailLine(const QcState& state, bool loadFailed);

/// Which keys do what. There is no help panel; this row is the only thing
/// that makes the bindings discoverable.
std::string formatHotkeyLine();

/// Printed after the session ends: "rated 57/340 (P40 W10 F7) -> out.csv".
std::string formatSummary(const QcState& state, const std::string& outputPath);

} // namespace mriv::qc
