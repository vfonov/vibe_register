#include "render/Terminal.hpp"

#include <algorithm>
#include <iostream>
#include <utility>

#include <notcurses/notcurses.h>

namespace mriv::term
{

Terminal::~Terminal()
{
    destroy();
}

Terminal::Terminal(Terminal&& other) noexcept
    : nc_(std::exchange(other.nc_, nullptr))
{
}

Terminal& Terminal::operator=(Terminal&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        nc_ = std::exchange(other.nc_, nullptr);
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

bool Terminal::hasPixelSupport() const
{
    if (!nc_)
        return false;
    return notcurses_check_pixel_support(nc_) != NCPIXEL_NONE;
}

Terminal::PixelGeometry Terminal::pixelGeometry() const
{
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

bool Terminal::blit(const uint32_t* rgba, int w, int h)
{
    if (!nc_)
    {
        std::cerr << "mriv: blit() called before a successful init()\n";
        return false;
    }
    if (w <= 0 || h <= 0)
    {
        std::cerr << "mriv: blit() called with an empty image\n";
        return false;
    }

    // ncvisual_from_rgba()'s middle argument is rowstride in *bytes*, not
    // column count -- see mriv/HANDOFF.md sec 3.3.
    struct ncvisual* ncv = ncvisual_from_rgba(rgba, h, w * 4, w);
    if (!ncv)
    {
        std::cerr << "mriv: ncvisual_from_rgba() failed\n";
        return false;
    }

    ncvisual_options vopts{};
    vopts.scaling = NCSCALE_NONE;
    vopts.blitter = NCBLIT_PIXEL;

    struct ncplane* plane = ncvisual_blit(nc_, ncv, &vopts);
    if (!plane)
    {
        std::cerr << "mriv: ncvisual_blit() failed (no pixel-capable terminal?)\n";
        ncvisual_destroy(ncv);
        return false;
    }

    bool ok = notcurses_render(nc_) == 0;
    if (!ok)
        std::cerr << "mriv: notcurses_render() failed\n";

    ncvisual_destroy(ncv);
    return ok;
}

} // namespace mriv::term
