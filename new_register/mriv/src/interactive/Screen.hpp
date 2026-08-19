#pragma once

#include <cstdint>
#include <optional>
#include <string>

// Forward-declared only: this header must not force notcurses includes on
// consumers (see mriv/CLAUDE.md, header hygiene).
struct notcurses;
struct ncplane;

namespace mriv::term
{

struct FrameOverlay;

/// RAII wrapper around a *full* notcurses context for interactive mode.
///
/// This is the counterpart to render/Terminal, which wraps ncdirect for the
/// one-shot path, and the split is deliberate. Direct mode is right for
/// "print one image and exit" and wrong here: interactive mode redraws in
/// place, which means the previous frame's bitmap has to be released before
/// the next is drawn. Full notcurses owns the sprixel lifecycle and does
/// that; hand-rolling it over ncdirect's cursor movement would be
/// reimplementing the part of notcurses that exists for this. The
/// bitmap-clearing teardown that made direct mode necessary for the one-shot
/// path (see render/Terminal.hpp and mriv/HANDOFF.md) is not a problem here
/// in the sense that mattered there -- restoring the terminal on exit is the
/// wanted behaviour, not a bug to work around. But notcurses_stop()'s own
/// bitmap-clearing is documented as terminal-dependent, not guaranteed, so
/// destroy() does not lean on it either: it destroys the last image plane
/// and forces a render to flush that deletion to the terminal before
/// notcurses_stop() runs, the same way every other frame's deletion rides
/// along with the render that draws the next frame (drawFrame() below).
///
/// This class is deliberately kept free of decisions. Everything that can be
/// judged without a terminal lives in ViewState, Session, StatusLine and the
/// render pipeline, all of which are unit-tested; what remains here is glue
/// that cannot be exercised on a host with no TTY and no pixel protocol
/// (mriv/HANDOFF.md sec 3.9). Keep it that way -- logic added here is logic
/// that will not be tested.
///
/// No exceptions cross this boundary: failures are a bool return plus a
/// std::cerr message.
class Screen
{
public:
    Screen() = default;
    ~Screen();

    Screen(const Screen&) = delete;
    Screen& operator=(const Screen&) = delete;

    /// Take over the terminal on the alternate screen. Returns false and
    /// writes a diagnostic to std::cerr on failure.
    bool init();

    /// True if the terminal supports a pixel graphics protocol. Only
    /// meaningful after a successful init().
    bool hasPixelSupport() const;

    struct PixelGeometry
    {
        unsigned width = 0;
        unsigned height = 0;

        /// The terminal's cell size in pixels -- what interactive/Overlay.hpp
        /// needs to turn a pane's pixel rectangle into the row/column a
        /// marker belongs at. Zero when unknown, same as width/height.
        unsigned cellWidth = 0;
        unsigned cellHeight = 0;
    };

    /// The pixel box available to the image, after reserving `headerRows`
    /// text rows above it (the volume names plus the status line -- see
    /// interactive/Overlay.hpp) and the marker gutter -- one row below,
    /// interactive/Overlay's kMarkerColumns columns to the left. Callers
    /// must pass the same headerRows they pass to planOverlay(), or the
    /// image will not agree with where the header text actually ends.
    /// Zero-initialized before a successful init().
    PixelGeometry pixelGeometry(int headerRows) const;

    /// Draw one frame: the overlay's text (volume names, status line and
    /// the two active-pane markers) at their planned positions, then the
    /// image at overlay.imageRow/imageColumn. The previous frame's image
    /// plane is destroyed first so its bitmap is released rather than left
    /// stacked underneath.
    bool drawFrame(const FrameOverlay& overlay, const uint32_t* rgba, int w, int h);

    /// Block until a key is pressed. Returns std::nullopt when input ends
    /// (EOF or a read error), which the session treats as a clean exit.
    /// Key releases, special keys and mouse events are swallowed here so
    /// the session only ever sees plain characters.
    std::optional<char> readKey();

private:
    void destroy();

    notcurses* nc_ = nullptr;
    ncplane* imagePlane_ = nullptr;
};

} // namespace mriv::term
