#pragma once

// Key codes and key results shared by every interactive state driven through
// runSession() (interactive/Session.hpp) and read by Screen::readKey(). Kept
// free of nr_core so the terminal layer (mriv_term) can be linked without the
// MINC stack -- mriv_qc does exactly that.

namespace mriv::term
{

/// What a keypress did, so the caller knows whether a repaint is due.
enum class KeyResult
{
    /// The key is not bound, or it asked for something already true (the
    /// current axis, a slice past either end, a volume already active).
    /// Nothing changed; do not repaint.
    Ignored,
    /// State changed; the caller should render a new frame.
    Changed,
    /// The user asked to leave interactive mode.
    Quit,
    /// The user asked to save the frame already on screen. Deliberately
    /// distinct from Changed: nothing in ViewState changed, so redrawing
    /// would cost a slice render and a bitmap upload for no visible
    /// difference. The caller saves whatever it last drew instead.
    Screenshot,
};

/// The arrow keys, carried through the session as the control codes
/// readline binds to the same movements (Ctrl-P/N/B/F). Screen translates
/// notcurses' synthetic key ids into these, so ViewState, Session and the
/// tests can all stay on plain chars instead of gaining a key type for
/// four bindings. Nothing else in the key map uses the control range, and
/// a user who types Ctrl-B on purpose gets the movement it names anyway.
constexpr char kKeyUp    = '\x10';
constexpr char kKeyDown  = '\x0e';
constexpr char kKeyLeft  = '\x02';
constexpr char kKeyRight = '\x06';

/// A terminal resize, or a request to just redraw. Carried the same way as
/// the arrows: Screen::readKey() maps notcurses' NCKEY_RESIZE to this rather
/// than handling the resize itself, since recomputing the display box and
/// rebuilding the frame needs the render pipeline, which Screen deliberately
/// does not have (mriv/HANDOFF.md sec 3.9). It changes nothing in ViewState
/// -- the picture is unchanged, only the terminal's geometry is -- so
/// handleKey() reports Changed unconditionally and lets the caller redo the
/// layout against whatever ViewState already holds. Bound to Ctrl-L, the
/// traditional Unix "redraw the screen" keystroke, which is deliberate: a
/// user who mashes it when a repaint looks stuck gets exactly the recovery
/// they expect, for free.
constexpr char kKeyResize = '\x0c';

/// Paging and jump keys, carried the same way as the arrows. Screen maps
/// NCKEY_HOME/END/PGUP/PGDOWN to these control codes (Ctrl-A/E, the readline
/// line-start/end keys, and Ctrl-U/D, the vi half-page keys). ViewState
/// binds none of them, so mriv ignores them; mriv_qc uses them to move
/// through the image list.
constexpr char kKeyHome     = '\x01';
constexpr char kKeyEnd      = '\x05';
constexpr char kKeyPageUp   = '\x15';
constexpr char kKeyPageDown = '\x04';

} // namespace mriv::term
