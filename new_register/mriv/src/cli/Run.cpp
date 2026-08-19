#include "cli/Run.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include <unistd.h>

#include "cli/ColourMapArg.hpp"
#include "cli/InteractiveDecision.hpp"
#include "cli/Options.hpp"
#include "cli/SliceSelection.hpp"
#include "cli/VolumeInfo.hpp"
#include "interactive/FramePlan.hpp"
#include "interactive/Overlay.hpp"
#include "interactive/Screen.hpp"
#include "interactive/Session.hpp"
#include "interactive/StatusLine.hpp"
#include "interactive/ViewState.hpp"
#include "render/FrameBuilder.hpp"
#include "render/PixelProtocol.hpp"
#include "render/Screenshot.hpp"
#include "render/SliceGeometry.hpp"
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

// The first volume is the one being judged and the rest are usually
// references it is compared against, so it gets the colour map that shows
// intensity structure and they stay grayscale. Overridden by --colourmap.
ColourMapType defaultColourMapFor(size_t volumeIndex)
{
    return volumeIndex == 0 ? ColourMapType::Spectral : ColourMapType::GrayScale;
}

// A loaded volume and the path it came from, kept together so the status
// line and the captions can name the right column.
struct LoadedVolume
{
    Volume volume;
    std::string path;
};

// Load every file, reporting and skipping the ones that fail. One bad path
// in `mriv *.mnc` should not cost the user everything else in the glob.
std::vector<LoadedVolume> loadVolumes(const std::vector<std::string>& paths, std::ostream& err)
{
    std::vector<LoadedVolume> volumes;
    volumes.reserve(paths.size());

    for (const auto& path : paths)
    {
        LoadedVolume loaded;
        loaded.path = path;
        try
        {
            loaded.volume.load(path);
        }
        catch (const std::exception& e)
        {
            err << "mriv: failed to load '" << path << "': " << e.what() << "\n";
            continue;
        }
        volumes.push_back(std::move(loaded));
    }

    return volumes;
}

// Per-volume intensity range and colour map. --range and --invert apply to
// every volume; --colourmap is either one name for all of them or one per
// file (parseArgs() has already rejected any other count).
bool buildDisplays(const Options& options,
                   const std::vector<LoadedVolume>& volumes,
                   std::vector<VolumeDisplay>& displays,
                   std::ostream& err)
{
    displays.clear();
    displays.reserve(volumes.size());

    for (size_t i = 0; i < volumes.size(); ++i)
    {
        const Volume& vol = volumes[i].volume;

        VolumeDisplay display;
        if (options.hasRange)
        {
            display.rangeLow  = options.rangeLow;
            display.rangeHigh = options.rangeHigh;
        }
        else
        {
            display.rangeLow  = vol.computeQuantile(kAutoWindowLowQuantile);
            display.rangeHigh = vol.computeQuantile(kAutoWindowHighQuantile);
        }

        display.colourMap = defaultColourMapFor(i);
        if (!options.colourMapArgs.empty())
        {
            const std::string& name = options.colourMapArgs.size() == 1
                                          ? options.colourMapArgs.front()
                                          : options.colourMapArgs[i];
            auto map = resolveColourMapArg(name);
            if (!map.has_value())
            {
                // parseArgs() already validated this, so this is defensive.
                err << "mriv: internal error: invalid colourmap '" << name << "'\n";
                return false;
            }
            display.colourMap = *map;
        }

        display.invertColourMap = options.invert;
        displays.push_back(display);
    }

    return true;
}

// The starting cursor: --slice positions the requested axis, the other two
// start at their midpoint. Resolved against the first volume, whose index
// space the shared cursor lives in.
glm::ivec3 resolveStartCursor(const Options& options, const Volume& vol)
{
    glm::ivec3 cursor(vol.dimensions.x / 2, vol.dimensions.y / 2, vol.dimensions.z / 2);

    int viewIndex = viewIndexForAxis(options.axis).value_or(0);
    auto selection = parseSliceArg(options.sliceArg);
    if (!selection.has_value())
        return cursor; // parseArgs() validated this already.

    int slice = resolveSliceIndex(*selection, sliceCountForView(viewIndex, vol.dimensions));
    switch (viewIndex)
    {
        case 1:  cursor.x = slice; break;
        case 2:  cursor.y = slice; break;
        default: cursor.z = slice; break;
    }
    return cursor;
}

ViewState buildViewState(const Options& options,
                         const std::vector<LoadedVolume>& volumes,
                         std::vector<VolumeDisplay> displays)
{
    std::vector<glm::ivec3> dimensions;
    dimensions.reserve(volumes.size());
    for (const auto& loaded : volumes)
        dimensions.push_back(loaded.volume.dimensions);

    return ViewState(std::move(dimensions),
                     options.views,
                     options.axis,
                     resolveStartCursor(options, volumes.front().volume),
                     std::move(displays));
}

std::vector<const Volume*> volumePointers(const std::vector<LoadedVolume>& volumes)
{
    std::vector<const Volume*> pointers;
    pointers.reserve(volumes.size());
    for (const auto& loaded : volumes)
        pointers.push_back(&loaded.volume);
    return pointers;
}

std::vector<std::string> volumePaths(const std::vector<LoadedVolume>& volumes)
{
    std::vector<std::string> paths;
    paths.reserve(volumes.size());
    for (const auto& loaded : volumes)
        paths.push_back(loaded.path);
    return paths;
}

// The image width budget: the terminal's box, capped by --max-width.
int displayWidth(unsigned boxWidth, const Options& options)
{
    int width = static_cast<int>(boxWidth);
    if (options.maxWidth.has_value())
        width = std::min(width, *options.maxWidth);
    return width;
}

// Interactive mode: load every file, take over the terminal, and hand the
// loop to runSession(). Everything decided here -- which slices, which
// ranges, how the grid is fitted -- goes through the same ViewState,
// StatusLine and buildFrame() the tests cover; Screen is only the terminal
// glue.
int runInteractive(const Options& options, std::ostream& err)
{
    const bool verbose = debugEnabled();
    auto log = [&](const std::string& msg) {
        if (verbose)
            err << "[mriv debug] " << msg << "\n";
    };

    auto volumes = loadVolumes(options.files, err);
    if (volumes.empty())
        return 1;
    log("interactive: loaded " + std::to_string(volumes.size()) + " volume(s)");

    std::vector<VolumeDisplay> displays;
    if (!buildDisplays(options, volumes, displays, err))
        return 1;

    ViewState state = buildViewState(options, volumes, std::move(displays));
    auto pointers = volumePointers(volumes);
    auto paths    = volumePaths(volumes);

    // Anything printed while the alternate screen is up is wiped when the
    // terminal is restored, so a diagnostic has to wait for Screen to be
    // destroyed. Hence the inner scope and the deferred message.
    int status = 0;
    std::string deferredError;

    // The last frame drawn, kept so it can be put back on the terminal
    // after the alternate screen is gone -- see below.
    ResampledImage lastFrame;
    std::vector<std::string> lastHeader;
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
            auto nameLines = formatVolumeNameLines(paths);
            // One row per volume plus the status row below them -- fixed
            // for the whole session, so the image box does not shift
            // underneath the header while navigating.
            int headerRows = static_cast<int>(nameLines.size()) + 1;

            // Set by takeScreenshot() below, shown in place of the status
            // line's usual summary+legend for exactly one draw() call, then
            // cleared -- the status row is one reserved line (StatusLine.hpp),
            // so this replaces it rather than adding a row the way the range
            // prompt already does for editing.
            std::string screenshotMessage;

            auto draw = [&](const ViewState& current) {
                // Queried fresh on every frame, not once before the loop:
                // the terminal can be resized mid-session (kKeyResize,
                // interactive/ViewState.hpp), and a box captured before the
                // loop would leave the image sized for a screen that no
                // longer exists. pixelGeometry() itself forces notcurses to
                // sync the plane to any pending resize before reading it
                // (interactive/Screen.cpp) -- callers here don't need to.
                auto box = screen.pixelGeometry(headerRows);
                int maxW = displayWidth(box.width, options);
                int maxH = static_cast<int>(box.height);
                log("interactive: display box " + std::to_string(maxW) + "x"
                    + std::to_string(maxH));

                FrameTracks tracks;
                auto frame = buildFrame(planFrame(current, pointers, maxW, maxH, options.scale),
                                        &tracks);
                if (frame.width <= 0 || frame.height <= 0)
                    return false;

                auto header = nameLines;
                if (screenshotMessage.empty())
                {
                    header.push_back(formatStatusLine(current, paths));
                }
                else
                {
                    header.push_back(screenshotMessage);
                    screenshotMessage.clear();
                }
                auto overlay = planOverlay(header, tracks, current.activeViewRow(),
                                          current.activeVolume(), frame.height,
                                          static_cast<int>(box.cellWidth),
                                          static_cast<int>(box.cellHeight));

                if (!screen.drawFrame(overlay, frame.pixels.data(), frame.width, frame.height))
                    return false;

                lastFrame  = std::move(frame);
                lastHeader = nameLines;
                lastHeader.push_back(formatSummaryLine(current, paths));
                return true;
            };

            auto keys = [&]() { return screen.readKey(); };

            // Saves whatever draw() last put on screen -- KeyResult::Screenshot
            // never touches ViewState, so there is no new frame to render,
            // only the last one to write out (mriv/PLAN.md, Interactive mode).
            // draw() is still called once afterward, not to re-render the
            // picture (identical pixels either way) but to put the result on
            // the status line -- otherwise a save with no on-screen
            // confirmation looks like the key did nothing.
            auto takeScreenshot = [&]() {
                if (lastFrame.width <= 0 || lastFrame.height <= 0)
                    return;
                std::string path = saveScreenshot(lastFrame.pixels.data(),
                                                  lastFrame.width, lastFrame.height);
                screenshotMessage = path.empty()
                    ? std::string("Screenshot failed -- see stderr")
                    : ("Screenshot saved: " + path);
                log(screenshotMessage);
                draw(state);
            };

            status = runSession(state, keys, draw, takeScreenshot);
        }
    }

    if (!deferredError.empty())
        err << deferredError << "\n";

    // Leave the last view on the terminal. The alternate screen is gone by
    // now, taking its contents with it, so the frame is re-emitted through
    // the one-shot ncdirect path -- the same code that prints an image and
    // exits, and the only one proven not to have its bitmaps wiped on
    // teardown (see render/Terminal.hpp). Best effort: failing to put the
    // picture back is not worth a non-zero exit after a clean session.
    if (status == 0 && lastFrame.width > 0 && lastFrame.height > 0)
    {
        Terminal terminal;
        if (terminal.initCli(stdout) && terminal.hasPixelSupport())
        {
            // The alternate screen restored the cursor to wherever it was
            // before the session started, not necessarily row 0 -- home it
            // first so a tall (--scale > 1) image always has a full
            // terminal's worth of room below it rather than scrolling and
            // overlapping this reprint's own text (mriv/HANDOFF.md).
            terminal.moveCursorHome();
            for (const auto& line : lastHeader)
                terminal.printLine(line);
            terminal.blit(lastFrame.pixels.data(), lastFrame.width, lastFrame.height);
        }
    }

    return status;
}

// One-shot mode: render the same grid interactive mode would show, blit it
// once, and exit. This is the "cat for medical images" path.
int runOneShot(const Options& options, std::ostream& out, std::ostream& err)
{
    const bool verbose = debugEnabled();
    auto log = [&](const std::string& msg) {
        if (verbose)
            err << "[mriv debug] " << msg << "\n";
    };

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

    // Checked before any volume is loaded: pixel support is a property of
    // the terminal, so a failure here should not cost a full load and
    // render first.
    log("checking pixel support...");
    if (!terminal.hasPixelSupport())
    {
        log("hasPixelSupport=false");
        err << "mriv: this terminal has no pixel graphics protocol "
               "(Kitty, sixel, or iTerm2). Try Kitty, Ghostty, WezTerm, iTerm2, or "
               "Konsole.\n";
        return options.requirePixels ? 1 : 0;
    }
    log("hasPixelSupport=true");

    auto volumes = loadVolumes(options.files, err);
    if (volumes.empty())
        return 1;

    std::vector<VolumeDisplay> displays;
    if (!buildDisplays(options, volumes, displays, err))
        return 1;

    ViewState state = buildViewState(options, volumes, std::move(displays));

    auto box  = terminal.pixelGeometry();
    int maxW  = displayWidth(box.width, options);
    int maxH  = static_cast<int>(box.height);

    auto frame = buildFrame(planFrame(state, volumePointers(volumes), maxW, maxH, options.scale));
    log("frame " + std::to_string(frame.width) + "x" + std::to_string(frame.height));
    if (frame.width <= 0 || frame.height <= 0)
    {
        err << "mriv: rendered frame is empty\n";
        return 1;
    }

    // Caption the columns when there is more than one, so they can be told
    // apart. A single-file invocation stays pure image bytes -- that is the
    // "cat for medical images" case, where a caption is just noise.
    if (volumes.size() > 1)
        terminal.printLine(formatCaption(volumePaths(volumes)));

    return terminal.blit(frame.pixels.data(), frame.width, frame.height) ? 0 : 1;
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

    // Interactive mode needs a terminal to read keys from; see
    // cli/InteractiveDecision.hpp for the rules. Decided before --info so
    // that asking for both is refused rather than silently resolved.
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

    // --info prints metadata for each file and exits before touching
    // rendering or the terminal at all.
    if (parsed.options.info)
    {
        int succeeded = 0;
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

    return runOneShot(parsed.options, out, err);
}

} // namespace mriv::term
