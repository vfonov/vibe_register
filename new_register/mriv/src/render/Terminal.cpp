#include "render/Terminal.hpp"

#include <algorithm>
#include <iostream>
#include <ostream>
#include <utility>

#include <notcurses/notcurses.h>

#include "render/Encode.hpp"

namespace mriv::term
{

Terminal::~Terminal()
{
    destroy();
}

Terminal::Terminal(Terminal&& other) noexcept
    : nc_(std::exchange(other.nc_, nullptr)),
      out_(std::exchange(other.out_, nullptr)),
      forcedProtocol_(other.forcedProtocol_)
{
}

Terminal& Terminal::operator=(Terminal&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        nc_ = std::exchange(other.nc_, nullptr);
        out_ = std::exchange(other.out_, nullptr);
        forcedProtocol_ = other.forcedProtocol_;
    }
    return *this;
}

void Terminal::destroy()
{
    if (nc_)
    {
        notcurses_stop(nc_);
        nc_ = nullptr;
    }
    // Test-mode state is just a raw pointer to a stream; nothing to tear down.
    out_ = nullptr;
    forcedProtocol_ = PixelProtocol::None;
}

bool Terminal::init(FILE* outFile, uint64_t optionFlags)
{
    destroy();

    notcurses_options opts{};
    opts.flags = optionFlags;

    nc_ = notcurses_init(&opts, outFile);
    if (!nc_)
    {
        std::cerr << "mriv: failed to initialize terminal (notcurses_init)\n";
        return false;
    }
    return true;
}

bool Terminal::initCli(FILE* outFile)
{
    return init(outFile, NCOPTION_CLI_MODE | NCOPTION_SUPPRESS_BANNERS | NCOPTION_DRAIN_INPUT);
}

Terminal::Terminal(std::ostream& out, PixelProtocol forcedProtocol)
    : out_(&out), forcedProtocol_(forcedProtocol)
{
}

bool Terminal::hasPixelSupport() const
{
    if (out_)
        return forcedProtocol_ != PixelProtocol::None;
    if (!nc_)
        return false;
    return notcurses_check_pixel_support(nc_) != NCPIXEL_NONE;
}

Terminal::PixelGeometry Terminal::pixelGeometry() const
{
    if (out_)
    {
        // Synthetic generous box for test mode.
        return PixelGeometry{4096, 4096};
    }

    if (!nc_)
        return PixelGeometry{0, 0};

    unsigned pxy = 0, pxx = 0, celldimy = 0, celldimx = 0, maxbmapy = 0, maxbmapx = 0;
    ncplane_pixel_geom(notcurses_stdplane_const(nc_), &pxy, &pxx, &celldimy, &celldimx,
                        &maxbmapy, &maxbmapx);

    // maxbmapy/maxbmapx are 0 when the terminal reports no limit -- only
    // clamp when non-zero (mriv/HANDOFF.md sec 5.5).
    PixelGeometry geom;
    geom.width  = maxbmapx != 0 ? std::min(pxx, maxbmapx) : pxx;
    geom.height = maxbmapy != 0 ? std::min(pxy, maxbmapy) : pxy;
    return geom;
}

unsigned Terminal::cursorRow() const
{
    if (!nc_)
        return 0;
    unsigned y = 0, x = 0;
    ncplane_cursor_yx(notcurses_stdplane_const(nc_), &y, &x);
    return y;
}

bool Terminal::blit(const uint32_t* rgba, int w, int h)
{
    if (!nc_ && !out_)
    {
        std::cerr << "mriv: blit() called before a successful init()\n";
        return false;
    }
    if (w <= 0 || h <= 0)
    {
        std::cerr << "mriv: blit() called with an empty image\n";
        return false;
    }

    // Test mode: bypass notcurses entirely and write deterministic bytes
    // to the injected stream.
    if (out_)
    {
        if (forcedProtocol_ == PixelProtocol::None)
        {
            std::cerr << "mriv: test-mode Terminal constructed with PixelProtocol::None\n";
            return false;
        }

        std::string bytes = encodePixels(forcedProtocol_,
                                         reinterpret_cast<const std::uint8_t*>(rgba),
                                         static_cast<std::size_t>(w),
                                         static_cast<std::size_t>(h));
        if (bytes.empty())
        {
            std::cerr << "mriv: encodePixels() produced no output\n";
            return false;
        }

        out_->write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return out_->good();
    }

    // ncvisual_from_rgba()'s middle argument is rowstride in *bytes*, not
    // column count -- see mriv/HANDOFF.md sec 3.3.
    struct ncvisual* ncv = ncvisual_from_rgba(rgba, h, w * 4, w);
    if (!ncv)
    {
        std::cerr << "mriv: ncvisual_from_rgba() failed\n";
        return false;
    }

    // With no ncplane given, ncvisual_options::y/x place the *new* plane
    // relative to the standard plane's origin -- NOT at the current
    // cursor position. Left at their zero-initialized default, the image
    // is drawn at row 0 of the std plane, which in CLI/scrolling mode is
    // wherever the physical cursor happened to be when notcurses_init()
    // ran (NCOPTION_PRESERVE_CURSOR seeds the std plane's virtual cursor
    // from the physical one at that point) -- but only by coincidence if
    // nothing else has been printed since. Query the std plane's current
    // cursor row explicitly so the image lands where the caller's output
    // actually is, rather than silently drawing over row 0 and leaving
    // no visible trace once the shell prints its next prompt at the old
    // cursor position (NCOPTION_PRESERVE_CURSOR / _NO_CLEAR_BITMAPS keep
    // that old position around).
    struct ncplane* stdPlane = notcurses_stdplane(nc_);
    unsigned cursorY = 0, cursorX = 0;
    ncplane_cursor_yx(stdPlane, &cursorY, &cursorX);

    ncvisual_options vopts{};
    vopts.scaling = NCSCALE_NONE;
    vopts.blitter = NCBLIT_PIXEL;
    vopts.y = static_cast<int>(cursorY);
    vopts.x = 0;

    struct ncplane* plane = ncvisual_blit(nc_, ncv, &vopts);
    if (!plane)
    {
        std::cerr << "mriv: ncvisual_blit() failed (no pixel-capable terminal?)\n";
        ncvisual_destroy(ncv);
        return false;
    }

    // Advance the std plane's cursor past the image so a scrolling
    // caller's next output (e.g. the shell prompt after we exit) lands
    // below it instead of overwriting it in place.
    unsigned rows = 0;
    ncplane_dim_yx(plane, &rows, nullptr);
    ncplane_cursor_move_yx(stdPlane, static_cast<int>(cursorY + rows), 0);

    bool ok = notcurses_render(nc_) == 0;
    if (!ok)
        std::cerr << "mriv: notcurses_render() failed\n";

    ncvisual_destroy(ncv);
    return ok;
}

} // namespace mriv::term
