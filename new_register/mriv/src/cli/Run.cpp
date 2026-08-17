#include "cli/Run.hpp"

#include <algorithm>
#include <iostream>
#include <istream>
#include <ostream>
#include <string>

#include "cli/ColourMapArg.hpp"
#include "cli/Options.hpp"
#include "cli/SliceSelection.hpp"
#include "cli/VolumeInfo.hpp"
#include "cli/WindowLevel.hpp"
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

// Percentile range for the default intensity window. Named per
// mriv/HANDOFF.md sec 6: there is no parent default to inherit, and
// 2%/98% is a sane choice for MRI. Used unless --window/--level override it.
constexpr double kAutoWindowLowQuantile  = 0.02;
constexpr double kAutoWindowHighQuantile = 0.98;

// Build VolumeRenderParams from parsed options, using either the explicit
// --window/--level pair or the percentile auto range.
bool buildRenderParams(const Options& options,
                       const Volume& vol,
                       VolumeRenderParams& params,
                       std::ostream& err)
{
    if (options.hasWindowLevel)
    {
        auto range = windowLevelToRange(options.window, options.level);
        params.valueMin = range.valueMin;
        params.valueMax = range.valueMax;
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
    auto viewIndexOpt = viewIndexForAxis(options.axis);
    if (!viewIndexOpt.has_value())
    {
        err << "mriv: internal error: invalid axis '" << options.axis << "'\n";
        return false;
    }
    int viewIndex = *viewIndexOpt;

    int dimAlongAxis = viewIndex == 0 ? vol.dimensions.z
                       : viewIndex == 1 ? vol.dimensions.x
                                        : vol.dimensions.y;

    auto sliceSelection = parseSliceArg(options.sliceArg);
    if (!sliceSelection.has_value())
    {
        err << "mriv: internal error: invalid slice '" << options.sliceArg << "'\n";
        return false;
    }
    int sliceIndex = resolveSliceIndex(*sliceSelection, dimAlongAxis);

    VolumeRenderParams params;
    if (!buildRenderParams(options, vol, params, err))
        return false;

    RenderedSlice slice = renderSlice(vol, params, viewIndex, sliceIndex);
    if (slice.width <= 0 || slice.height <= 0)
    {
        err << "mriv: rendered slice is empty\n";
        return false;
    }

    if (!terminal.hasPixelSupport())
    {
        err << "mriv: this terminal has no pixel graphics protocol "
               "(Kitty, sixel, or iTerm2). Try Kitty, Ghostty, WezTerm, iTerm2, or "
               "Konsole.\n";
        return options.requirePixels ? false : true;
    }

    auto axes     = aspectAxesForView(viewIndex);
    double aspect = vol.slicePixelAspect(axes.u, axes.v);

    auto box = terminal.pixelGeometry();
    int maxW = static_cast<int>(box.width);
    int maxH = static_cast<int>(box.height);
    if (options.maxWidth.has_value())
        maxW = std::min(maxW, *options.maxWidth);

    auto display = resampleToDisplay(slice, aspect, maxW, maxH);

    return terminal.blit(display.pixels.data(), display.width, display.height);
}

} // namespace

int run(int argc, char** argv, std::istream& /*in*/, std::ostream& out, std::ostream& err)
{
    auto parsed = parseArgs(argc, argv, err);
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

    // --info prints metadata and exits before touching rendering.
    if (parsed.options.info)
    {
        out << formatVolumeInfo(vol, path);
        return 0;
    }

    Terminal terminal;
    if (std::getenv("MRIV_TEST_RENDER"))
    {
        // Test mode: force Kitty protocol bytes into `out` so tests can
        // inspect escape-sequence structure without a real terminal.
        terminal = Terminal(out, PixelProtocol::Kitty);
    }
    else
    {
        if (!terminal.initCli(stdout))
            return 1;
    }

    if (!renderAndBlitVolume(vol, parsed.options, terminal, err))
        return 1;

    return 0;
}

} // namespace mriv::term
