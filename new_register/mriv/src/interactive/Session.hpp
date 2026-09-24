#pragma once

#include <functional>
#include <optional>

#include "interactive/Keys.hpp"

namespace mriv::term
{

/// Returns the next keypress, or std::nullopt when input has ended -- EOF,
/// a closed terminal, or a read error. Ending input is a normal way to
/// leave the session, not a failure.
using KeySource = std::function<std::optional<char>()>;

/// Draws the given state. Returns false if the frame could not be drawn,
/// which ends the session with a non-zero status.
template <typename State>
using FrameSink = std::function<bool(const State&)>;

/// Saves whatever is currently on screen. Called for KeyResult::Screenshot,
/// which never changes the state -- there is nothing new to draw, only the
/// last successfully drawn frame to write out, so this is a distinct sink
/// from FrameSink rather than a state to render.
using ScreenshotSink = std::function<void()>;

namespace detail
{
/// Keeps `draw`'s parameter out of template argument deduction, so State is
/// deduced from `state` alone and any callable (a lambda, std::ref(...))
/// converts to the FrameSink afterwards. std::type_identity is C++20.
template <typename T>
struct NonDeduced
{
    using type = T;
};
} // namespace detail

/// Run the interactive loop until the user quits or input ends.
///
/// `State` is anything with `KeyResult handleKey(char)` -- mriv's ViewState,
/// or mriv_qc's QcState. The loop is the same for both.
///
/// An initial frame is drawn before any key is read, so the user sees the
/// slice they asked for without pressing anything. After that a frame is
/// drawn only when a key actually changed the state: a repaint costs a full
/// slice render plus a bitmap upload, and holding 'j' at the top of a stack
/// should be free rather than a redraw storm. A screenshot request costs
/// neither -- it saves the frame already drawn rather than triggering a new
/// one, so it calls `screenshot` and loops back for the next key without
/// touching `draw`.
///
/// `screenshot` defaults to a no-op so callers that do not care about it
/// (most tests) do not need to pass one.
///
/// Returns 0 on a clean exit and 1 if a frame failed to draw.
template <typename State>
int runSession(State& state, const KeySource& nextKey,
               const typename detail::NonDeduced<FrameSink<State>>::type& draw,
               const ScreenshotSink& screenshot = ScreenshotSink())
{
    if (!draw(state))
        return 1;

    for (;;)
    {
        std::optional<char> key = nextKey();
        if (!key.has_value())
            return 0;

        KeyResult result = state.handleKey(*key);
        if (result == KeyResult::Quit)
            return 0;
        if (result == KeyResult::Screenshot)
        {
            if (screenshot)
                screenshot();
            continue;
        }
        if (result == KeyResult::Ignored)
            continue;

        if (!draw(state))
            return 1;
    }
}

} // namespace mriv::term
