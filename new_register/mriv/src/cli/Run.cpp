#include "cli/Run.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <istream>
#include <ostream>
#include <string>

#include <unistd.h>

#include "cli/ColourMapArg.hpp"
#include "cli/InteractiveDecision.hpp"
#include "cli/Options.hpp"
#include "cli/SliceSelection.hpp"
#include "cli/VolumeInfo.hpp"
#include "render/PixelProtocol.hpp"
#include "interactive/Screen.hpp"
#include "interactive/Session.hpp"
#include "interactive/StatusLine.hpp"
#include "render/SliceGeometry.hpp"
#include "render/SlicePipeline.hpp"
#include "render/Terminal.hpp"

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

// Fill in a SliceRequest's intensity and colour-map fields from parsed
// options, using either the explicit --range pair or the percentile auto
// range.
bool applyDisplayOptions(const Options& options,
                         const Volume& vol,
                         SliceRequest& request,
                         std::ostream& err)
{
    if (options.hasRange)
    {
        request.valueMin = options.rangeLow;
        request.valueMax = options.rangeHigh;
    }
    else
    {
        request.valueMin = vol.computeQuantile(kAutoWindowLowQuantile);
        request.valueMax = vol.computeQuantile(kAutoWindowHighQuantile);
    }

    if (!options.colourMapArgs.empty())
    {
        auto map = resolveColourMapArg(options.colourMapArgs.front());
        if (!map.has_value())
        {
            // parseArgs() already validated this, so this is defensive.
            err << "mriv: internal error: invalid colourmap '"
                << options.colourMapArgs.front() << "'\n";
            return false;
        }
        request.colourMap = *map;
    }
    request.invertColourMap = options.invert;
    return true;
}

// Render a single volume slice, resample it to the terminal display box,
// and blit it through the supplied Terminal. `box` is the terminal's pixel
// geometry, queried once by the caller -- it cannot change during a
// one-shot run, and probing it per file costs a throwaway ncvisual each
// time (see Terminal::pixelGeometry()).
//
// Every row of a multi-file strip gets the full height budget, exactly as
// a single-file render does: terminals scroll, so there is no need to make
// N images share one screen, and a volume should not change size according
// to how many siblings happened to be on the command line.
bool renderAndBlitVolume(const Volume& vol,
                         const Options& options,
                         Terminal& terminal,
                         Terminal::PixelGeometry box,
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

    int dimAlongAxis = sliceCountForView(viewIndex, vol.dimensions);
    log("dimAlongAxis=" + std::to_string(dimAlongAxis));

    auto sliceSelection = parseSliceArg(options.sliceArg);
    if (!sliceSelection.has_value())
    {
        err << "mriv: internal error: invalid slice '" << options.sliceArg << "'\n";
        return false;
    }

    SliceRequest request;
    request.viewIndex  = viewIndex;
    request.sliceIndex = resolveSliceIndex(*sliceSelection, dimAlongAxis);
    request.scale      = options.scale;
    log("sliceArg='" + options.sliceArg + "' -> sliceIndex=" + std::to_string(request.sliceIndex));

    if (!applyDisplayOptions(options, vol, request, err))
        return false;
    log("valueMin=" + std::to_string(request.valueMin)
        + " valueMax=" + std::to_string(request.valueMax));

    log("terminal pixel geometry width=" + std::to_string(box.width)
        + " height=" + std::to_string(box.height));

    request.maxWidth  = static_cast<int>(box.width);
    request.maxHeight = static_cast<int>(box.height);
    if (options.maxWidth.has_value())
    {
        request.maxWidth = std::min(request.maxWidth, *options.maxWidth);
        log("--max-width applied; maxW=" + std::to_string(request.maxWidth));
    }

    log("calling renderSliceForDisplay with scale=" + std::to_string(request.scale) + "...");
    auto display = renderSliceForDisplay(vol, request);
    log("display image " + std::to_string(display.width) + "x" + std::to_string(display.height)
        + " pixel count=" + std::to_string(display.pixels.size()));
    if (display.width <= 0 || display.height <= 0)
    {
        err << "mriv: rendered slice is empty\n";
        return false;
    }

    log("calling terminal.blit...");
    bool blitOk = terminal.blit(display.pixels.data(), display.width, display.height);
    log("terminal.blit returned " + std::string(blitOk ? "true" : "false"));

    return blitOk;
}

// Resolve the starting slice for `axis` from --slice.
int resolveStartSlice(const Options& options, const Volume& vol, int viewIndex)
{
    int dimAlongAxis = sliceCountForView(viewIndex, vol.dimensions);
    auto selection = parseSliceArg(options.sliceArg);
    if (!selection.has_value())
        return dimAlongAxis / 2; // parseArgs() validated this already.
    return resolveSliceIndex(*selection, dimAlongAxis);
}

// Interactive mode: load the one volume, take over the terminal, and hand
// the loop to runSession(). Everything decided here -- which slice, which
// range, how the image is fitted -- goes through the same ViewState,
// StatusLine and renderSliceForDisplay() the tests cover; Screen is only
// the terminal glue.
int runInteractive(const Options& options, std::ostream& err)
{
    const bool verbose = debugEnabled();
    auto log = [&](const std::string& msg) {
        if (verbose)
            err << "[mriv debug] " << msg << "\n";
    };

    const std::string& path = options.files.front();

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
    log("interactive: loaded " + path);

    // The colour map, inversion and --scale are fixed for the session; the
    // view, slice and range come from the ViewState on every frame.
    SliceRequest base;
    if (!applyDisplayOptions(options, vol, base, err))
        return 1;
    base.scale = options.scale;

    int viewIndex = viewIndexForAxis(options.axis).value_or(0);
    glm::ivec3 cursor(vol.dimensions.x / 2, vol.dimensions.y / 2, vol.dimensions.z / 2);
    switch (viewIndex)
    {
        case 1:  cursor.x = resolveStartSlice(options, vol, viewIndex); break;
        case 2:  cursor.y = resolveStartSlice(options, vol, viewIndex); break;
        default: cursor.z = resolveStartSlice(options, vol, viewIndex); break;
    }

    VolumeDisplay display;
    display.rangeLow        = base.valueMin;
    display.rangeHigh       = base.valueMax;
    display.colourMap       = base.colourMap;
    display.invertColourMap = base.invertColourMap;

    ViewState state({vol.dimensions}, options.views, options.axis, cursor, {display});

    // Anything printed while the alternate screen is up is wiped when the
    // terminal is restored, so a diagnostic has to wait for Screen to be
    // destroyed. Hence the inner scope and the deferred message.
    int status = 0;
    std::string deferredError;
    {
        Screen screen;
        if (!screen.init())
            return 1;

        if (!screen.hasPixelSupport())
        {
            deferredError = "mriv: this terminal has no pixel graphics protocol "
                            "(Kitty, sixel, or iTerm2). Try Kitty, Ghostty, WezTerm, "
                            "iTerm2, or Konsole.";
            status = 1;
        }
        else
        {
            auto box = screen.pixelGeometry();
            int maxW = static_cast<int>(box.width);
            if (options.maxWidth.has_value())
                maxW = std::min(maxW, *options.maxWidth);
            int maxH = static_cast<int>(box.height);
            log("interactive: display box " + std::to_string(maxW) + "x" + std::to_string(maxH));

            auto draw = [&](const ViewState& current) {
                SliceRequest request = base;
                request.viewIndex  = current.viewIndex();
                request.sliceIndex = current.sliceIndex();
                request.valueMin   = current.display(0).rangeLow;
                request.valueMax   = current.display(0).rangeHigh;
                request.maxWidth   = maxW;
                request.maxHeight  = maxH;

                auto image = renderSliceForDisplay(vol, request);
                if (image.width <= 0 || image.height <= 0)
                    return false;

                return screen.drawFrame(formatStatusLine(current, path),
                                        image.pixels.data(), image.width, image.height);
            };

            auto keys = [&]() { return screen.readKey(); };

            status = runSession(state, keys, draw);
        }
    }

    if (!deferredError.empty())
        err << deferredError << "\n";

    return status;
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

    // Interactive mode needs a terminal to read keys from and a single
    // volume to navigate; see cli/InteractiveDecision.hpp for the rules.
    // Decided before --info so that asking for both is refused rather than
    // silently resolved.
    auto interactiveDecision = decideInteractive(parsed.options, isatty(STDOUT_FILENO) != 0);
    if (!interactiveDecision.refusal.empty())
    {
        err << "mriv: " << interactiveDecision.refusal << "\n";
        return 1;
    }
    if (interactiveDecision.interactive)
    {
        log("entering interactive mode");
        return runInteractive(parsed.options, err);
    }

    // An unreadable file is skipped rather than aborting the whole run, so
    // one bad path in `mriv *.mnc` doesn't cost the user everything else in
    // the glob. Only a run that renders (or reports on) nothing at all is an
    // error. `succeeded` tracks that across both the --info and render paths.
    int succeeded = 0;

    // --info prints metadata for each file and exits before touching
    // rendering or the terminal at all.
    if (parsed.options.info)
    {
        for (const auto& path : parsed.options.files)
        {
            log("loading volume for --info: " + path);
            Volume vol;
            try
            {
                vol.load(path);
            }
            catch (const std::exception& e)
            {
                err << "mriv: failed to load '" << path << "': " << e.what() << "\n";
                continue;
            }
            out << formatVolumeInfo(vol, path);
            ++succeeded;
        }
        return succeeded > 0 ? 0 : 1;
    }

    log("initializing Terminal");
    Terminal terminal;
    if (const char* testRender = std::getenv("MRIV_TEST_RENDER"))
    {
        // MRIV_TEST_RENDER=none forces a pixel-less test terminal so the
        // no-pixel-support and --require-pixels branches are reachable from
        // Layer C. Any other non-empty value keeps the Kitty behaviour every
        // existing test relies on.
        bool none = std::string(testRender) == "none";
        log("MRIV_TEST_RENDER set -> test-mode terminal, protocol="
            + std::string(none ? "None" : "Kitty"));
        terminal = Terminal(out, none ? PixelProtocol::None : PixelProtocol::Kitty);
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

    // Checked once, before any volume is loaded: pixel support is a property
    // of the terminal, so testing it per file produced N copies of this
    // message and wasted a full load + renderSlice() on each one.
    log("checking pixel support...");
    if (!terminal.hasPixelSupport())
    {
        log("hasPixelSupport=false");
        err << "mriv: this terminal has no pixel graphics protocol "
               "(Kitty, sixel, or iTerm2). Try Kitty, Ghostty, WezTerm, iTerm2, or "
               "Konsole.\n";
        return parsed.options.requirePixels ? 1 : 0;
    }
    log("hasPixelSupport=true");

    // Queried once: the terminal cannot resize during a one-shot run, and in
    // real mode each probe allocates a throwaway ncvisual.
    auto box = terminal.pixelGeometry();

    // Multiple files render as a strip: one row per file, in argument order.
    // Terminal::blit() places each image at the current cursor position and
    // advances past it, so calling it repeatedly stacks the images vertically
    // with no extra bookkeeping needed here.
    const bool labelRows = parsed.options.files.size() > 1;
    for (const auto& path : parsed.options.files)
    {
        log("loading volume: " + path);
        Volume vol;
        try
        {
            vol.load(path);
        }
        catch (const std::exception& e)
        {
            err << "mriv: failed to load '" << path << "': " << e.what() << "\n";
            continue;
        }
        log("volume loaded dims=" + std::to_string(vol.dimensions.x) + "x"
            + std::to_string(vol.dimensions.y) + "x" + std::to_string(vol.dimensions.z));

        // Caption each row of a strip so the images can be told apart. A
        // single-file run stays pure image bytes -- that is the "cat for
        // medical images" case, where a caption is just noise.
        if (labelRows)
            terminal.printLine(path);

        log("entering renderAndBlitVolume for " + path);
        if (!renderAndBlitVolume(vol, parsed.options, terminal, box, err))
        {
            log("renderAndBlitVolume returned false -> exiting 1");
            return 1;
        }
        ++succeeded;
    }

    log("rendered " + std::to_string(succeeded) + " of "
        + std::to_string(parsed.options.files.size()) + " files");
    return succeeded > 0 ? 0 : 1;
}

} // namespace mriv::term
