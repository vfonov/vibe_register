#include "cli/Run.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <istream>
#include <ostream>
#include <string>

#include "cli/ColourMapArg.hpp"
#include "cli/Options.hpp"
#include "cli/SliceSelection.hpp"
#include "cli/VolumeInfo.hpp"
#include "render/PixelProtocol.hpp"
#include "render/Resample.hpp"
#include "render/SliceGeometry.hpp"
#include "render/Terminal.hpp"

#include "SliceRenderer.h"
#include "Volume.h"

namespace mriv::term
{

namespace
{

bool debugEnabled()
{
    const char* v = std::getenv("MRIV_DEBUG");
    return v && v[0] != '\0';
}

// Percentile range for the default intensity window. Named per
// mriv/HANDOFF.md sec 6: there is no parent default to inherit, and
// 2%/98% is a sane choice for MRI. Used unless --range overrides it.
constexpr double kAutoWindowLowQuantile  = 0.02;
constexpr double kAutoWindowHighQuantile = 0.98;

// Build VolumeRenderParams from parsed options, using either the explicit
// --range pair or the percentile auto range.
bool buildRenderParams(const Options& options,
                       const Volume& vol,
                       VolumeRenderParams& params,
                       std::ostream& err)
{
    if (options.hasRange)
    {
        params.valueMin = options.rangeLow;
        params.valueMax = options.rangeHigh;
    }
    else
    {
        params.valueMin = vol.computeQuantile(kAutoWindowLowQuantile);
        params.valueMax = vol.computeQuantile(kAutoWindowHighQuantile);
    }

    if (!options.colourMapArg.empty())
    {
        auto map = resolveColourMapArg(options.colourMapArg);
        if (!map.has_value())
        {
            // parseArgs() already validated this, so this is defensive.
            err << "mriv: internal error: invalid colourmap '" << options.colourMapArg << "'\n";
            return false;
        }
        params.colourMap = *map;
    }
    params.invertColourMap = options.invert;
    return true;
}

// Render a single volume slice, resample it to the terminal display box,
// and blit it through the supplied Terminal.
bool renderAndBlitVolume(const Volume& vol,
                         const Options& options,
                         Terminal& terminal,
                         std::ostream& err)
{
    const bool verbose = debugEnabled();
    auto log = [&](const std::string& msg) {
        if (verbose)
            err << "[mriv debug] " << msg << "\n";
    };

    log("starting renderAndBlitVolume");

    auto viewIndexOpt = viewIndexForAxis(options.axis);
    if (!viewIndexOpt.has_value())
    {
        err << "mriv: internal error: invalid axis '" << options.axis << "'\n";
        return false;
    }
    int viewIndex = *viewIndexOpt;
    log("axis='" + std::string(1, options.axis) + "' -> viewIndex=" + std::to_string(viewIndex));

    int dimAlongAxis = viewIndex == 0 ? vol.dimensions.z
                       : viewIndex == 1 ? vol.dimensions.x
                                        : vol.dimensions.y;
    log("dimAlongAxis=" + std::to_string(dimAlongAxis));

    auto sliceSelection = parseSliceArg(options.sliceArg);
    if (!sliceSelection.has_value())
    {
        err << "mriv: internal error: invalid slice '" << options.sliceArg << "'\n";
        return false;
    }
    int sliceIndex = resolveSliceIndex(*sliceSelection, dimAlongAxis);
    log("sliceArg='" + options.sliceArg + "' -> sliceIndex=" + std::to_string(sliceIndex));

    VolumeRenderParams params;
    if (!buildRenderParams(options, vol, params, err))
        return false;
    log("valueMin=" + std::to_string(params.valueMin) + " valueMax=" + std::to_string(params.valueMax));

    log("calling renderSlice...");
    RenderedSlice slice = renderSlice(vol, params, viewIndex, sliceIndex);
    log("renderSlice returned " + std::to_string(slice.width) + "x" + std::to_string(slice.height));
    if (slice.width <= 0 || slice.height <= 0)
    {
        err << "mriv: rendered slice is empty\n";
        return false;
    }

    log("checking pixel support...");
    bool hasPixels = terminal.hasPixelSupport();
    log("hasPixelSupport=" + std::string(hasPixels ? "true" : "false"));

    if (!hasPixels)
    {
        err << "mriv: this terminal has no pixel graphics protocol "
               "(Kitty, sixel, or iTerm2). Try Kitty, Ghostty, WezTerm, iTerm2, or "
               "Konsole.\n";
        return options.requirePixels ? false : true;
    }

    auto axes     = aspectAxesForView(viewIndex);
    double aspect = vol.slicePixelAspect(axes.u, axes.v);
    log("aspect axes u=" + std::to_string(axes.u) + " v=" + std::to_string(axes.v)
        + " aspect=" + std::to_string(aspect));

    auto box = terminal.pixelGeometry();
    log("terminal pixel geometry width=" + std::to_string(box.width)
        + " height=" + std::to_string(box.height));

    int maxW = static_cast<int>(box.width);
    int maxH = static_cast<int>(box.height);
    if (options.maxWidth.has_value())
    {
        maxW = std::min(maxW, *options.maxWidth);
        log("--max-width applied; maxW=" + std::to_string(maxW));
    }

    log("calling resampleToDisplay with scale=" + std::to_string(options.scale) + "...");
    auto display = resampleToDisplay(slice, aspect, maxW, maxH, options.scale);
    log("resampled to " + std::to_string(display.width) + "x" + std::to_string(display.height)
        + " pixel count=" + std::to_string(display.pixels.size()));

    log("calling terminal.blit...");
    bool blitOk = terminal.blit(display.pixels.data(), display.width, display.height);
    log("terminal.blit returned " + std::string(blitOk ? "true" : "false"));

    return blitOk;
}

} // namespace

int run(int argc, char** argv, std::istream& /*in*/, std::ostream& out, std::ostream& err)
{
    const bool verbose = debugEnabled();
    auto log = [&](const std::string& msg) {
        if (verbose)
            err << "[mriv debug] " << msg << "\n";
    };

    log("parseArgs starting");
    auto parsed = parseArgs(argc, argv, err);
    log("parseArgs ok=" + std::string(parsed.ok ? "true" : "false")
        + " help=" + std::string(parsed.options.help ? "true" : "false")
        + " version=" + std::string(parsed.options.version ? "true" : "false")
        + " info=" + std::string(parsed.options.info ? "true" : "false")
        + " files=" + std::to_string(parsed.options.files.size()));

    if (!parsed.ok)
        return 1;

    if (parsed.options.help)
    {
        printHelp(out);
        return 0;
    }

    if (parsed.options.version)
    {
        printVersion(out);
        return 0;
    }

    if (parsed.options.files.empty())
    {
        err << "mriv: no input files given\n";
        printHelp(out);
        return 1;
    }

    if (parsed.options.files.size() > 1)
    {
        err << "mriv: multiple files are not yet supported (coming in a later milestone)\n";
        return 1;
    }

    const std::string& path = parsed.options.files[0];
    log("loading volume: " + path);

    Volume vol;
    try
    {
        vol.load(path);
    }
    catch (const std::exception& e)
    {
        err << "mriv: failed to load '" << path << "': " << e.what() << "\n";
        return 1;
    }
    log("volume loaded dims=" + std::to_string(vol.dimensions.x) + "x"
        + std::to_string(vol.dimensions.y) + "x" + std::to_string(vol.dimensions.z));

    // --info prints metadata and exits before touching rendering.
    if (parsed.options.info)
    {
        out << formatVolumeInfo(vol, path);
        return 0;
    }

    log("initializing Terminal");
    Terminal terminal;
    if (std::getenv("MRIV_TEST_RENDER"))
    {
        log("MRIV_TEST_RENDER set -> test-mode Kitty terminal");
        terminal = Terminal(out, PixelProtocol::Kitty);
    }
    else
    {
        log("MRIV_TEST_RENDER not set -> real notcurses initCli");
        if (!terminal.initCli(stdout))
        {
            log("Terminal::initCli failed");
            return 1;
        }
        log("Terminal::initCli succeeded cursorRow=" + std::to_string(terminal.cursorRow()));
    }

    log("entering renderAndBlitVolume");
    if (!renderAndBlitVolume(vol, parsed.options, terminal, err))
    {
        log("renderAndBlitVolume returned false -> exiting 1");
        return 1;
    }

    log("renderAndBlitVolume returned true -> exiting 0");
    return 0;
}

} // namespace mriv::term
