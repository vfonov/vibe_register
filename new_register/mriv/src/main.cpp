/// mriv — terminal-based slice viewer for MINC2/NIfTI volumes.

#include <algorithm>
#include <iostream>

#include "cli/ColourMapArg.hpp"
#include "cli/Options.hpp"
#include "cli/SliceSelection.hpp"
#include "cli/VolumeInfo.hpp"
#include "cli/WindowLevel.hpp"
#include "render/Resample.hpp"
#include "render/SliceGeometry.hpp"
#include "render/Terminal.hpp"

#include "SliceRenderer.h"
#include "Volume.h"

namespace
{

// Percentile range for the default intensity window. Named per
// mriv/HANDOFF.md sec 6: there is no parent default to inherit, and
// 2%/98% is a sane choice for MRI. Used unless --window/--level override it.
constexpr double kAutoWindowLowQuantile  = 0.02;
constexpr double kAutoWindowHighQuantile = 0.98;

} // namespace

int main(int argc, char** argv)
{
    auto parsed = mriv::term::parseArgs(argc, argv);
    if (!parsed.ok)
        return 1;

    if (parsed.options.help)
    {
        mriv::term::printHelp();
        return 0;
    }

    if (parsed.options.version)
    {
        mriv::term::printVersion();
        return 0;
    }

    if (parsed.options.files.empty())
    {
        std::cerr << "mriv: no input files given\n";
        mriv::term::printHelp();
        return 1;
    }

    if (parsed.options.files.size() > 1)
    {
        std::cerr << "mriv: multiple files are not yet supported (coming in a later milestone)\n";
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
        std::cerr << "mriv: failed to load '" << path << "': " << e.what() << "\n";
        return 1;
    }

    // --info prints metadata and exits before touching notcurses (PLAN.md).
    if (parsed.options.info)
    {
        std::cout << mriv::term::formatVolumeInfo(vol, path);
        return 0;
    }

    auto viewIndexOpt = mriv::term::viewIndexForAxis(parsed.options.axis);
    if (!viewIndexOpt.has_value())
    {
        // parseArgs() already validates --axis, so this should be
        // unreachable; guard anyway rather than dereferencing.
        std::cerr << "mriv: internal error: invalid axis '" << parsed.options.axis << "'\n";
        return 1;
    }
    int viewIndex = *viewIndexOpt;

    int dimAlongAxis = viewIndex == 0 ? vol.dimensions.z
                       : viewIndex == 1 ? vol.dimensions.x
                                        : vol.dimensions.y;

    auto sliceSelection = mriv::term::parseSliceArg(parsed.options.sliceArg);
    if (!sliceSelection.has_value())
    {
        // parseArgs() already validates --slice; unreachable in practice.
        std::cerr << "mriv: internal error: invalid slice '" << parsed.options.sliceArg << "'\n";
        return 1;
    }
    int sliceIndex = mriv::term::resolveSliceIndex(*sliceSelection, dimAlongAxis);

    VolumeRenderParams params;
    if (parsed.options.hasWindowLevel)
    {
        auto range = mriv::term::windowLevelToRange(parsed.options.window, parsed.options.level);
        params.valueMin = range.valueMin;
        params.valueMax = range.valueMax;
    }
    else
    {
        params.valueMin = vol.computeQuantile(kAutoWindowLowQuantile);
        params.valueMax = vol.computeQuantile(kAutoWindowHighQuantile);
    }

    if (!parsed.options.colourMapArg.empty())
    {
        // parseArgs() already validated this name.
        params.colourMap = *mriv::term::resolveColourMapArg(parsed.options.colourMapArg);
    }
    params.invertColourMap = parsed.options.invert;

    RenderedSlice slice = renderSlice(vol, params, viewIndex, sliceIndex);
    if (slice.width <= 0 || slice.height <= 0)
    {
        std::cerr << "mriv: rendered slice is empty\n";
        return 1;
    }

    mriv::term::Terminal terminal;
    if (!terminal.initCli(stdout))
        return 1;

    if (!terminal.hasPixelSupport())
    {
        std::cerr << "mriv: this terminal has no pixel graphics protocol "
                      "(Kitty, sixel, or iTerm2). Try Kitty, Ghostty, WezTerm, iTerm2, or "
                      "Konsole.\n";
        return parsed.options.requirePixels ? 1 : 0;
    }

    auto axes     = mriv::term::aspectAxesForView(viewIndex);
    double aspect = vol.slicePixelAspect(axes.u, axes.v);

    auto box = terminal.pixelGeometry();
    int maxW = static_cast<int>(box.width);
    int maxH = static_cast<int>(box.height);
    if (parsed.options.maxWidth.has_value())
        maxW = std::min(maxW, *parsed.options.maxWidth);

    auto display = mriv::term::resampleToDisplay(slice, aspect, maxW, maxH);

    if (!terminal.blit(display.pixels.data(), display.width, display.height))
        return 1;

    return 0;
}
