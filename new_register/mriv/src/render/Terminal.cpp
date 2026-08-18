#include "render/Terminal.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <ostream>
#include <utility>

#include <notcurses/direct.h>
#include <notcurses/notcurses.h>

#include "render/Encode.hpp"

namespace mriv::term
{

namespace
{

bool debugEnabled()
{
    const char* v = std::getenv("MRIV_DEBUG");
    return v && v[0] != '\0';
}

void debugLog(const std::string& msg)
{
    if (debugEnabled())
        std::cerr << "[mriv debug Terminal] " << msg << "\n";
}

} // namespace

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
        ncdirect_stop(nc_);
        nc_ = nullptr;
    }
    // Test-mode state is just a raw pointer to a stream; nothing to tear down.
    out_ = nullptr;
    forcedProtocol_ = PixelProtocol::None;
}

bool Terminal::init(FILE* outFile, uint64_t optionFlags)
{
    destroy();

    debugLog("ncdirect_init with flags=0x" + std::to_string(optionFlags));
    nc_ = ncdirect_init(nullptr, outFile, optionFlags);
    if (!nc_)
    {
        std::cerr << "mriv: failed to initialize terminal (ncdirect_init)\n";
        return false;
    }
    int pix = ncdirect_check_pixel_support(nc_);
    debugLog("ncdirect_check_pixel_support returned " + std::to_string(pix));
    return true;
}

bool Terminal::initCli(FILE* outFile)
{
    // Direct mode has no alternate-screen/clear-bitmaps/preserve-cursor
    // knobs to compose (that was full notcurses's NCOPTION_CLI_MODE) --
    // it never manages a "screen" abstraction in the first place. Just
    // drain input mriv has no intention of reading.
    return init(outFile, NCDIRECT_OPTION_DRAIN_INPUT);
}

Terminal::Terminal(std::ostream& out, PixelProtocol forcedProtocol)
    : out_(&out), forcedProtocol_(forcedProtocol)
{
}

bool Terminal::hasPixelSupport() const
{
    if (out_)
    {
        debugLog("test-mode hasPixelSupport -> "
                 + std::string(forcedProtocol_ != PixelProtocol::None ? "true" : "false"));
        return forcedProtocol_ != PixelProtocol::None;
    }
    if (!nc_)
        return false;
    int pix = ncdirect_check_pixel_support(nc_);
    debugLog("real hasPixelSupport -> " + std::to_string(pix));
    return pix != NCPIXEL_NONE;
}

Terminal::PixelGeometry Terminal::pixelGeometry() const
{
    if (out_)
    {
        debugLog("test-mode pixelGeometry -> 4096x4096");
        return PixelGeometry{4096, 4096};
    }

    if (!nc_)
    {
        debugLog("no nc_ pixelGeometry -> 0x0");
        return PixelGeometry{0, 0};
    }

    // ncdirectf_geom() requires a non-null loaded frame (it has no
    // "just tell me the terminal's capabilities" overload the way full
    // notcurses's ncplane_pixel_geom() does) -- so probe with a
    // throwaway 1x1 RGBA image purely to read the terminal's cell
    // geometry back out.
    uint32_t probePixel = 0;
    struct ncvisual* probe = ncvisual_from_rgba(&probePixel, 1, sizeof(probePixel), 1);
    if (!probe)
    {
        debugLog("pixelGeometry: probe ncvisual_from_rgba failed -> 0x0");
        return PixelGeometry{0, 0};
    }

    ncvisual_options vopts{};
    vopts.scaling = NCSCALE_NONE;
    vopts.blitter = NCBLIT_PIXEL;

    ncvgeom geom{};
    int rc = ncdirectf_geom(nc_, probe, &vopts, &geom);
    ncvisual_destroy(probe);
    if (rc != 0)
    {
        debugLog("ncdirectf_geom failed -> 0x0");
        return PixelGeometry{0, 0};
    }

    unsigned dimx = ncdirect_dim_x(nc_);
    unsigned dimy = ncdirect_dim_y(nc_);
    debugLog("ncdirectf_geom cdimy=" + std::to_string(geom.cdimy) + " cdimx="
             + std::to_string(geom.cdimx) + " maxpixely=" + std::to_string(geom.maxpixely)
             + " maxpixelx=" + std::to_string(geom.maxpixelx) + " termdim=" + std::to_string(dimx)
             + "x" + std::to_string(dimy));

    unsigned width  = dimx * geom.cdimx;
    unsigned height = dimy * geom.cdimy;

    // maxpixely/maxpixelx are 0 when the terminal reports no limit -- only
    // clamp when non-zero (mriv/HANDOFF.md sec 5.5).
    PixelGeometry out;
    out.width  = geom.maxpixelx != 0 ? std::min(width, geom.maxpixelx) : width;
    out.height = geom.maxpixely != 0 ? std::min(height, geom.maxpixely) : height;
    debugLog("computed pixelGeometry width=" + std::to_string(out.width)
             + " height=" + std::to_string(out.height));
    return out;
}

unsigned Terminal::cursorRow() const
{
    if (!nc_)
        return 0;
    unsigned y = 0, x = 0;
    if (ncdirect_cursor_yx(nc_, &y, &x) != 0)
    {
        debugLog("ncdirect_cursor_yx failed -> cursorRow 0");
        return 0;
    }
    return y;
}

bool Terminal::printLine(const std::string& text)
{
    debugLog("printLine: " + text);

    if (out_)
    {
        *out_ << text << "\n";
        return out_->good();
    }

    if (!nc_)
    {
        std::cerr << "mriv: printLine() called before a successful init()\n";
        return false;
    }

    // ncdirect_putstr() takes an optional channel argument; 0 means "leave
    // the terminal's current attributes alone", which is what we want for a
    // plain caption.
    std::string line = text + "\n";
    return ncdirect_putstr(nc_, 0, line.c_str()) >= 0;
}

bool Terminal::blit(const uint32_t* rgba, int w, int h)
{
    debugLog("blit called w=" + std::to_string(w) + " h=" + std::to_string(h));
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
        bool ok = out_->good();
        debugLog("test-mode blit wrote " + std::to_string(bytes.size()) + " bytes ok="
                 + std::string(ok ? "true" : "false"));
        return ok;
    }

    // ncvisual_from_rgba()'s middle argument is rowstride in *bytes*, not
    // column count -- see mriv/HANDOFF.md sec 3.3.
    struct ncvisual* ncv = ncvisual_from_rgba(rgba, h, w * 4, w);
    if (!ncv)
    {
        std::cerr << "mriv: ncvisual_from_rgba() failed\n";
        return false;
    }
    debugLog("ncvisual_from_rgba succeeded");

    ncvisual_options vopts{};
    vopts.scaling = NCSCALE_NONE;
    vopts.blitter = NCBLIT_PIXEL;

    // ncdirectf_render() + ncdirect_raster_frame() place the image at the
    // current cursor position and advance past it themselves -- unlike
    // full notcurses's ncvisual_blit(), there is no separate y/x to set
    // and no manual cursor bookkeeping to get wrong (see the placement
    // bug this replaced, mriv/HANDOFF.md's "round 1" entry).
    debugLog("ncdirectf_render");
    ncdirectv* rendered = ncdirectf_render(nc_, ncv, &vopts);
    if (!rendered)
    {
        std::cerr << "mriv: ncdirectf_render() failed (no pixel-capable terminal?)\n";
        ncvisual_destroy(ncv);
        return false;
    }
    debugLog("ncdirectf_render succeeded");

    // ncdirect_raster_frame() frees `rendered` on all paths.
    bool ok = ncdirect_raster_frame(nc_, rendered, NCALIGN_LEFT) == 0;
    debugLog("ncdirect_raster_frame returned " + std::string(ok ? "ok" : "failed"));
    if (!ok)
        std::cerr << "mriv: ncdirect_raster_frame() failed\n";

    ncvisual_destroy(ncv);
    return ok;
}

} // namespace mriv::term
