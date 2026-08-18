#pragma once

#include <cstdint>
#include <cstdio>
#include <iosfwd>

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

private:
    void destroy();

    ncdirect* nc_ = nullptr;

    std::ostream* out_ = nullptr;
    PixelProtocol forcedProtocol_ = PixelProtocol::None;
};

} // namespace mriv::term
