#pragma once

#include <cstdint>
#include <cstdio>

// Forward-declared only: this header must not force notcurses includes on
// consumers (see mriv/CLAUDE.md, header hygiene). <notcurses/notcurses.h>
// stays inside Terminal.cpp.
struct notcurses;

namespace mriv::term
{

/// RAII wrapper around a notcurses context. No exceptions cross this
/// boundary: failures are reported via a bool return plus a std::cerr
/// message.
class Terminal
{
public:
    Terminal() = default;
    ~Terminal();

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;
    Terminal(Terminal&& other) noexcept;
    Terminal& operator=(Terminal&& other) noexcept;

    /// Initialize notcurses. `outFile` lets tests redirect output away
    /// from the real terminal (e.g. tmpfile()); pass nullptr to use
    /// stdout. `optionFlags` is a bitmask of NCOPTION_*.
    bool init(FILE* outFile, uint64_t optionFlags = 0);

    /// Initialize notcurses for non-interactive CLI use: no alternate
    /// screen, no clearing, cursor preserved, banners suppressed, stray
    /// input drained. This is the "cat for medical images" mode -- mriv
    /// prints one image and exits, it does not take over the terminal.
    bool initCli(FILE* outFile);

    /// True if the terminal supports a pixel graphics protocol
    /// (Kitty/sixel/iTerm2). Only meaningful after a successful init().
    bool hasPixelSupport() const;

    struct PixelGeometry
    {
        unsigned width;
        unsigned height;
    };

    /// The pixel box available for a blit on the standard plane, already
    /// clamped to the terminal's own maximum bitmap size when it reports
    /// one. Zero-initialized if called before a successful init().
    PixelGeometry pixelGeometry() const;

    /// Blit a packed RGBA (0xAABBGGRR) buffer of size (w,h) to the
    /// terminal with NCBLIT_PIXEL and flush it to the output. Returns
    /// false and writes a diagnostic to std::cerr on failure.
    bool blit(const uint32_t* rgba, int w, int h);

private:
    void destroy();

    notcurses* nc_ = nullptr;
};

} // namespace mriv::term
