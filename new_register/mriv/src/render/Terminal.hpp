#pragma once

#include <cstdint>
#include <cstdio>
#include <iosfwd>
#include <string>

#include "render/PixelProtocol.hpp"

// Forward-declared only: this header must not force notcurses includes on
// consumers (see mriv/CLAUDE.md, header hygiene). <notcurses/direct.h>
// stays inside Terminal.cpp.
struct ncdirect;

namespace mriv::term
{

/// RAII wrapper around a notcurses *direct-mode* (ncdirect) context.
///
/// This is deliberately ncdirect, not full notcurses: mriv's non-interactive
/// path prints one image and exits, exactly the "cat for medical images"
/// use case ncdirect is documented for. Full notcurses (ncplane/ncvisual_blit
/// + notcurses_render/notcurses_stop) manages a whole-screen abstraction and,
/// per <notcurses/notcurses.h>'s own doc comment on NCOPTION_NO_CLEAR_BITMAPS,
/// may wipe just-drawn bitmaps on notcurses_stop() "even if this is set" --
/// confirmed against a real Kitty terminal (see mriv/HANDOFF.md). Direct mode
/// has no such teardown: it never owns a "screen" to restore, so there is
/// nothing for ncdirect_stop() to clear. An interactive full-screen mode
/// (PLAN.md milestone M5) is a different problem with a live redraw loop and
/// will want full notcurses again; that is a separate class when it lands,
/// not a fallback bolted onto this one.
///
/// No exceptions cross this boundary: failures are reported via a bool
/// return plus a std::cerr message.
class Terminal
{
public:
    Terminal() = default;
    ~Terminal();

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;
    Terminal(Terminal&& other) noexcept;
    Terminal& operator=(Terminal&& other) noexcept;

    /// Initialize direct-mode notcurses. `outFile` lets tests redirect
    /// output away from the real terminal (e.g. tmpfile()); pass nullptr
    /// to use stdout. `optionFlags` is a bitmask of NCDIRECT_OPTION_*.
    /// Despite <notcurses/direct.h>'s doc comment that 'fp' "must be a
    /// tty", ncdirect_init() does not enforce this at runtime (verified
    /// experimentally against this sandbox's non-tty tmpfile()) -- the
    /// tmpfile()-based test strategy below still works.
    bool init(FILE* outFile, uint64_t optionFlags = 0);

    /// Initialize direct-mode notcurses for non-interactive CLI use,
    /// draining stray input since mriv does not read it. This is the
    /// "cat for medical images" mode -- mriv prints one image and exits,
    /// it does not take over the terminal.
    bool initCli(FILE* outFile);

    /// Test-mode constructor: skip notcurses entirely and write a
    /// forced pixel protocol directly to the supplied stream. This is
    /// the seam that makes cheap CI tests possible -- no real terminal,
    /// no environment probing, just deterministic escape-sequence bytes.
    /// The stream must outlive the Terminal.
    explicit Terminal(std::ostream& out, PixelProtocol forcedProtocol);

    /// True if the terminal supports a pixel graphics protocol
    /// (Kitty/sixel/iTerm2). Only meaningful after a successful init();
    /// always true for test-mode terminals with a non-None protocol.
    bool hasPixelSupport() const;

    struct PixelGeometry
    {
        unsigned width;
        unsigned height;
    };

    /// The pixel box available for a blit, computed from the terminal's
    /// cell geometry (ncdirect_dim_x/y * the cell size ncdirectf_geom()
    /// reports) and clamped to the terminal's own maximum bitmap size
    /// when it reports one. Zero-initialized if called before a
    /// successful init(), or if the terminal doesn't report a cell pixel
    /// size (no pixel protocol). In test mode this reports a generous
    /// synthetic box (4096x4096).
    PixelGeometry pixelGeometry() const;

    /// Write a line of plain text (a trailing newline is added) at the
    /// current cursor position. Used for the per-file captions above each
    /// row of a multi-file strip.
    ///
    /// This goes through Terminal rather than straight to std::cout so all
    /// output -- text and image bytes alike -- travels one path: in test
    /// mode both land in the injected stream in the right order, and in
    /// real mode both go through the same FILE* rather than interleaving
    /// std::cout with ncdirect's stdio writes. Returns false and writes a
    /// diagnostic to std::cerr on failure.
    bool printLine(const std::string& text);

    /// Blit a packed RGBA (0xAABBGGRR) buffer of size (w,h) at the
    /// current cursor position and flush it to the output. Placement and
    /// cursor advancement past the image are handled internally by
    /// ncdirect_raster_frame() -- unlike full notcurses, there is no
    /// manual y/x bookkeeping here. Returns false and writes a
    /// diagnostic to std::cerr on failure.
    bool blit(const uint32_t* rgba, int w, int h);

    /// The terminal's current cursor row, via ncdirect_cursor_yx(). This
    /// requires a round-trip terminal query; it is exposed for debug
    /// logging and tests, not used internally by blit() (see blit()'s
    /// comment). Returns 0 before a successful init(), or if the query
    /// fails (e.g. no reader on the far end, as with a tmpfile() in tests).
    unsigned cursorRow() const;

    /// Move the cursor to the terminal's top-left corner (absolute row 0,
    /// column 0) without touching any existing screen content. Used before
    /// the exit-time retained-frame reprint (cli/Run.cpp) so a tall image
    /// always has a full terminal height of room below it, regardless of
    /// where the cursor was left after the alternate screen was restored
    /// (see mriv/HANDOFF.md for why that position isn't row 0). Returns
    /// false and writes a diagnostic to std::cerr on failure; in test mode,
    /// writes a fixed "\x1b[H" marker to the injected stream instead of
    /// touching notcurses, so tests can pin call order against blit().
    bool moveCursorHome();

private:
    void destroy();

    ncdirect* nc_ = nullptr;

    std::ostream* out_ = nullptr;
    PixelProtocol forcedProtocol_ = PixelProtocol::None;
};

} // namespace mriv::term
