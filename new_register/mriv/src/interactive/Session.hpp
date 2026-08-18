#pragma once

#include <functional>
#include <optional>

#include "interactive/ViewState.hpp"

namespace mriv::term
{

/// Returns the next keypress, or std::nullopt when input has ended -- EOF,
/// a closed terminal, or a read error. Ending input is a normal way to
/// leave the session, not a failure.
using KeySource = std::function<std::optional<char>()>;

/// Draws the given state. Returns false if the frame could not be drawn,
/// which ends the session with a non-zero status.
using FrameSink = std::function<bool(const ViewState&)>;

/// Run the interactive loop until the user quits or input ends.
///
/// An initial frame is drawn before any key is read, so the user sees the
/// slice they asked for without pressing anything. After that a frame is
/// drawn only when a key actually changed the state: a repaint costs a full
/// slice render plus a bitmap upload, and holding 'j' at the top of a stack
/// should be free rather than a redraw storm.
///
/// Returns 0 on a clean exit and 1 if a frame failed to draw.
int runSession(ViewState& state, const KeySource& nextKey, const FrameSink& draw);

} // namespace mriv::term
