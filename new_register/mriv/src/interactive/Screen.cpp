#include "interactive/Screen.hpp"

#include <cstdlib>
#include <iostream>

#include <notcurses/notcurses.h>

#include "interactive/ViewState.hpp"
#include "interactive/Overlay.hpp"

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
        std::cerr << "[mriv debug] Screen: " << msg << "\n";
}

} // namespace

Screen::~Screen()
{
    destroy();
}

void Screen::destroy()
{
    if (imagePlane_)
    {
        ncplane_destroy(imagePlane_);
        imagePlane_ = nullptr;

        // Every other frame's plane-destruction is flushed to the terminal
        // by the render that draws the *next* frame (drawFrame() destroys
        // the previous plane, then renders). This is the last frame of the
        // session, so there is no next frame to carry it -- without a
        // render here, the deletion stays purely in notcurses' internal
        // model, notcurses_stop() restoring the primary screen does not
        // reliably clear a placed pixel-protocol image along with it
        // (terminal-dependent per notcurses' own docs, mriv/HANDOFF.md),
        // and the leftover image survives the switch back, overlapping
        // whatever runInteractive() prints there next.
        if (nc_ && notcurses_render(nc_) < 0)
            debugLog("notcurses_render (teardown flush) failed");
    }
    if (nc_)
    {
        notcurses_stop(nc_);
        nc_ = nullptr;
    }
}

bool Screen::init()
{
    notcurses_options opts{};
    // Banners would scroll the very image we are about to draw. The
    // alternate screen is the default and is wanted here: interactive mode
    // takes over the terminal and gives it back untouched on exit.
    opts.flags = NCOPTION_SUPPRESS_BANNERS;

    nc_ = notcurses_init(&opts, stdout);
    if (!nc_)
    {
        std::cerr << "mriv: failed to initialize the terminal for interactive mode\n";
        return false;
    }

    debugLog("notcurses_init succeeded");
    return true;
}

bool Screen::hasPixelSupport() const
{
    if (!nc_)
        return false;

    int support = notcurses_check_pixel_support(nc_);
    debugLog("notcurses_check_pixel_support returned " + std::to_string(support));
    return support > NCPIXEL_NONE;
}

Screen::PixelGeometry Screen::pixelGeometry(int headerRows) const
{
    PixelGeometry geometry;
    if (!nc_)
        return geometry;

    // notcurses only resizes the standard plane to match a new terminal size
    // during the *next* render or refresh -- never at the moment an
    // NCKEY_RESIZE event is read (notcurses_refresh(3): "if an NCKEY_RESIZE
    // event has been read and you're not yet ready to render," a render or
    // refresh is still required before the geometry is trustworthy). Reading
    // it here, unconditionally, is what makes the values below current
    // whether or not a resize just happened -- without it this function can
    // silently return the pre-resize box. notcurses_render() rather than
    // notcurses_refresh(): the latter's own man page says it clears the
    // screen first, which deletes placed Kitty-protocol images (see
    // drawFrame() below and mriv/HANDOFF.md), and for exactly this situation
    // recommends notcurses_render() instead. Re-rendering already-drawn,
    // unchanged content is cheap -- notcurses elides unchanged sprixel data
    // rather than resending it (the sprixelemissions/sprixelelisions stats
    // logged in drawFrame() are what that elision shows up as).
    if (notcurses_render(nc_) < 0)
        debugLog("notcurses_render (geometry sync) failed");

    ncplane* stdplane = notcurses_stdplane(nc_);
    unsigned rows = 0;
    unsigned cols = 0;
    ncplane_dim_yx(stdplane, &rows, &cols);

    unsigned pxy = 0, pxx = 0, celly = 0, cellx = 0, maxbmapy = 0, maxbmapx = 0;
    ncplane_pixel_geom(stdplane, &pxy, &pxx, &celly, &cellx, &maxbmapy, &maxbmapx);
    debugLog("ncplane_pixel_geom pxy=" + std::to_string(pxy) + " pxx=" + std::to_string(pxx)
             + " celly=" + std::to_string(celly) + " cellx=" + std::to_string(cellx)
             + " maxbmapy=" + std::to_string(maxbmapy) + " maxbmapx=" + std::to_string(maxbmapx));

    if (celly == 0 || cellx == 0)
        return geometry;

    geometry.cellWidth  = cellx;
    geometry.cellHeight = celly;

    // headerRows above the image, interactive/Overlay's marker row below it
    // and marker columns to its left -- see planOverlay() for what goes in
    // that space.
    unsigned reservedRows = static_cast<unsigned>(headerRows) + static_cast<unsigned>(kMarkerRows);
    unsigned reservedCols = static_cast<unsigned>(kMarkerColumns);

    unsigned imageRows = rows > reservedRows ? rows - reservedRows : 0;
    unsigned imageCols = cols > reservedCols ? cols - reservedCols : 0;
    geometry.height = imageRows * celly;
    geometry.width  = imageCols * cellx;

    // Clamp only when the terminal actually reports a limit -- zero means
    // "no limit known", not "no space".
    if (maxbmapy > 0 && geometry.height > maxbmapy)
        geometry.height = maxbmapy;
    if (maxbmapx > 0 && geometry.width > maxbmapx)
        geometry.width = maxbmapx;

    return geometry;
}

bool Screen::drawFrame(const FrameOverlay& overlay, const uint32_t* rgba, int w, int h)
{
    if (!nc_)
    {
        std::cerr << "mriv: drawFrame() called before a successful init()\n";
        return false;
    }
    if (!rgba || w <= 0 || h <= 0)
    {
        std::cerr << "mriv: refusing to draw an empty frame\n";
        return false;
    }

    // Destroy the previous frame's plane before drawing the next one, so
    // its bitmap is released rather than left stacked underneath.
    if (imagePlane_)
    {
        ncplane_destroy(imagePlane_);
        imagePlane_ = nullptr;
    }

    ncplane* stdplane = notcurses_stdplane(nc_);
    ncplane_erase(stdplane);
    for (const auto& cell : overlay.text)
    {
        if (ncplane_putstr_yx(stdplane, cell.row, cell.col, cell.text.c_str()) < 0)
            debugLog("ncplane_putstr_yx failed for \"" + cell.text + "\" (off-plane?)");
    }

    // ncvisual_from_rgba() takes the row stride in *bytes*, not pixels --
    // see mriv/HANDOFF.md sec 3.3.
    ncvisual* visual = ncvisual_from_rgba(rgba, h, w * static_cast<int>(sizeof(uint32_t)), w);
    if (!visual)
    {
        std::cerr << "mriv: failed to build an image for the terminal\n";
        return false;
    }

    ncvisual_options vopts{};
    vopts.n       = stdplane;
    vopts.y       = overlay.imageRow;
    vopts.x       = overlay.imageColumn;
    vopts.scaling = NCSCALE_NONE;
    vopts.blitter = NCBLIT_PIXEL;
    vopts.flags   = NCVISUAL_OPTION_CHILDPLANE;

    imagePlane_ = ncvisual_blit(nc_, visual, &vopts);
    ncvisual_destroy(visual);

    if (!imagePlane_)
    {
        std::cerr << "mriv: failed to draw the slice\n";
        return false;
    }

    if (notcurses_render(nc_) < 0)
    {
        std::cerr << "mriv: failed to render the frame\n";
        return false;
    }

    // A forced notcurses_refresh() was tried here to work around a real
    // mlterm/sixel bug (slice navigation updated the status text but never
    // the image -- see the MRIV_DEBUG counters below). It was reverted: its
    // own doc comment says it "clears the screen" before repainting, and on
    // a real Ghostty session (Kitty protocol) that turned the image
    // invisible for the *entire* interactive session instead -- clearing
    // the screen deletes a terminal's placed Kitty image along with it, so
    // the fix broke the terminal family it was previously working on. Do
    // not reintroduce this without confirming on both a sixel and a Kitty-
    // protocol terminal; see mriv/HANDOFF.md for the investigation.

    if (debugEnabled())
    {
        // sprixelemissions vs sprixelelisions tells us whether notcurses
        // actually sent fresh bitmap data this frame or decided the
        // terminal already had it -- the question that matters when a
        // redraw is visually silent on a sixel terminal but not on Kitty.
        ncstats stats{};
        notcurses_stats(nc_, &stats);
        debugLog("after render: renders=" + std::to_string(stats.renders)
                 + " writeouts=" + std::to_string(stats.writeouts)
                 + " sprixelemissions=" + std::to_string(stats.sprixelemissions)
                 + " sprixelelisions=" + std::to_string(stats.sprixelelisions)
                 + " sprixelbytes=" + std::to_string(stats.sprixelbytes)
                 + " raster_bytes=" + std::to_string(stats.raster_bytes)
                 + " planes=" + std::to_string(stats.planes));
    }

    return true;
}

std::optional<char> Screen::readKey()
{
    if (!nc_)
        return std::nullopt;

    for (;;)
    {
        ncinput input{};
        uint32_t id = notcurses_get_blocking(nc_, &input);

        if (id == static_cast<uint32_t>(-1) || id == NCKEY_EOF)
            return std::nullopt;

        // Terminals report both press and release; acting on both would
        // apply every key twice.
        if (input.evtype == NCTYPE_RELEASE)
            continue;

        // Translated rather than handled here, like Backspace and the
        // arrows below: recomputing the display box and rebuilding the
        // frame at the new size needs the render pipeline, which this
        // class deliberately does not have. Reading this event does *not*
        // by itself make the plane's geometry current -- notcurses only
        // applies a pending resize during the next render/refresh, not at
        // input-read time -- so no extra step belongs here; pixelGeometry()
        // forces that sync itself before every read (see its comment).
        if (id == NCKEY_RESIZE)
            return kKeyResize;

        // Escape is passed through as itself rather than translated to
        // 'q' here: what it means depends on state this class deliberately
        // does not have -- it closes the range prompt when one is open and
        // quits otherwise -- so ViewState decides.
        if (id == 0x1b)
            return '\x1b';

        // notcurses reports Backspace as a synthetic key above the Unicode
        // range, so it has to be translated before the filter below drops
        // it; without this the range prompt could be typed into but never
        // corrected.
        if (id == NCKEY_BACKSPACE)
            return '\b';

        if (id == NCKEY_ENTER)
            return '\r';

        // The arrows are the other half of the x/y/z and Tab bindings, and
        // notcurses reports them above the Unicode range like Backspace, so
        // they need translating before the filter below drops them. The
        // control codes they map to are ViewState's, not this class's
        // invention (interactive/ViewState.hpp).
        if (id == NCKEY_UP)
            return kKeyUp;
        if (id == NCKEY_DOWN)
            return kKeyDown;
        if (id == NCKEY_LEFT)
            return kKeyLeft;
        if (id == NCKEY_RIGHT)
            return kKeyRight;

        // The remaining special keys (function keys, page up/down, mouse)
        // live beyond Unicode and have no binding; swallow them rather than
        // passing junk on.
        if (id > 0x7f)
            continue;

        return static_cast<char>(id);
    }
}

} // namespace mriv::term
