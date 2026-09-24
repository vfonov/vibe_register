#include "qc/QcRun.hpp"

#include <algorithm>
#include <cstdio>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include <unistd.h>

#include "interactive/FrameOverlay.hpp"
#include "interactive/Screen.hpp"
#include "interactive/Session.hpp"
#include "qc/QcImage.hpp"
#include "qc/QcOptions.hpp"
#include "qc/QcState.hpp"
#include "qc/QcStatusLine.hpp"

#include "CSVHandler.h"

namespace mriv::qc
{

namespace
{

// Header, path/message, key legend.
constexpr int kTextRows = 3;

/// Records the same way new_qc builds them (QCApp): an existing results
/// file wins, so a second run resumes where the first stopped; otherwise
/// the chosen input source.
bool loadRecords(const QcOptions& options, QC::CSVHandler& csv, std::istream& in)
{
    if (csv.loadOutputCSV(options.output))
        return true;
    if (!options.inputCsv.empty())
        return csv.loadInputCSV(options.inputCsv);
    if (options.readStdin)
        return csv.loadImageList(in);
    return csv.loadImagePaths(options.images);
}

} // namespace

int runQc(int argc, char** argv, std::istream& in, std::ostream& out, std::ostream& err)
{
    using namespace mriv::term;

    auto parsed = parseQcArgs(argc, argv, err);
    if (!parsed.ok)
    {
        err << "Try 'mriv_qc --help'.\n";
        return 2;
    }
    const QcOptions& options = parsed.options;
    if (options.help)
    {
        printQcHelp(out);
        return 0;
    }
    if (options.version)
    {
        printQcVersion(out);
        return 0;
    }

    if (!isatty(STDOUT_FILENO))
    {
        err << "mriv_qc: stdout is not a terminal; mriv_qc is interactive only\n";
        return 1;
    }

    QC::CSVHandler csv;
    if (!loadRecords(options, csv, in) || csv.getRecordCount() == 0)
    {
        err << "mriv_qc: no images to rate\n";
        return 1;
    }

    // The list came down a pipe, so stdin is that pipe, now at EOF. Keys
    // have to come from the controlling terminal instead.
    if (options.readStdin && !isatty(STDIN_FILENO)
        && std::freopen("/dev/tty", "r", stdin) == nullptr)
    {
        err << "mriv_qc: cannot open /dev/tty for keyboard input\n";
        return 1;
    }

    QcState state(csv.getRecords(),
                  [&]() { return csv.saveOutputCSV(options.output); },
                  options.autoAdvance);

    // Anything printed while the alternate screen is up is wiped when the
    // terminal is restored (as in mriv's runInteractive), so errors wait.
    int status = 0;
    std::string deferredError;
    {
        Screen screen("mriv_qc");
        if (!screen.init())
            return 1;

        if (!screen.hasPixelSupport())
        {
            deferredError = "mriv_qc: this terminal has no pixel graphics protocol "
                            "(Kitty, sixel, or iTerm2). Try Kitty, Ghostty, WezTerm, "
                            "iTerm2, or Konsole.";
            status = 1;
        }
        else
        {
            // Decoded once per image, not per frame: a resize or a verdict
            // redraws the same picture.
            size_t loadedIndex = static_cast<size_t>(-1);
            std::optional<Image> loaded;

            auto draw = [&](const QcState& current) {
                if (current.index() != loadedIndex)
                {
                    loaded = loadImage(current.current().picture_path);
                    loadedIndex = current.index();
                }

                auto box = screen.pixelGeometry(kTextRows, 0);
                ResampledImage shown =
                    loaded ? fitImageToBox(*loaded, static_cast<int>(box.width),
                                           static_cast<int>(box.height), options.scale)
                           : ResampledImage{};

                // drawFrame() refuses an empty image, and the previous
                // picture must not linger under a "cannot load" line: one
                // black cell's worth of pixels clears it.
                if (shown.width <= 0 || shown.height <= 0)
                {
                    shown.width = std::max(1, static_cast<int>(box.cellWidth));
                    shown.height = std::max(1, static_cast<int>(box.cellHeight));
                    shown.pixels.assign(static_cast<size_t>(shown.width) * shown.height,
                                        0xFF000000u);
                }

                FrameOverlay overlay;
                overlay.text.push_back({0, 0, formatHeader(current)});
                overlay.text.push_back({1, 0, formatDetailLine(current, !loaded)});
                overlay.text.push_back({2, 0, formatHotkeyLine()});
                overlay.imageRow = kTextRows;
                // Centred with a cell offset rather than composed onto a
                // full-width black canvas: over SSH every frame's bitmap
                // crosses the wire, and padding is bytes for nothing.
                overlay.imageColumn = centreOffsetCells(static_cast<int>(box.width), shown.width,
                                                        static_cast<int>(box.cellWidth));
                return screen.drawFrame(overlay, shown.pixels.data(), shown.width, shown.height);
            };

            auto keys = [&]() { return screen.readKey(); };

            status = runSession(state, keys, draw);
        }
    }

    if (!deferredError.empty())
    {
        err << deferredError << "\n";
        return status;
    }

    // Last chance for a verdict whose save failed mid-session.
    if (state.dirty() && !csv.saveOutputCSV(options.output))
    {
        err << "mriv_qc: could not save " << options.output << "\n";
        status = 1;
    }

    out << formatSummary(state, options.output) << "\n";
    return status;
}

} // namespace mriv::qc
