#pragma once

#include <optional>
#include <string>
#include <vector>

namespace mriv::term
{

/// Parsed command-line state. See PLAN.md's CLI surface section for the
/// full flag list.
struct Options
{
    std::vector<std::string> files;
    bool help = false;
    bool version = false;
    bool info = false;

    /// 'x', 'y', or 'z'. Validated by parseArgs(); see
    /// render/SliceGeometry.hpp for the axis -> viewIndex mapping.
    char axis = 'z';

    /// Raw --slice argument ("n", "p%", or "mid"); validated by
    /// parseArgs() but resolved against a volume's dimensions later
    /// (see cli/SliceSelection.hpp).
    std::string sliceArg = "mid";

    /// Set when --window/--level were both given, overriding
    /// --auto-window (the default). See cli/WindowLevel.hpp.
    bool hasWindowLevel = false;
    double window = 0.0;
    double level = 0.0;

    /// Explicit --auto-window flag. Auto-window is already the default
    /// when neither --window nor --level is given; this flag exists so
    /// scripts can say so explicitly (PLAN.md's CLI surface). It is an
    /// error to combine it with --window/--level.
    bool autoWindow = false;

    /// Raw --colourmap argument; already validated by parseArgs() against
    /// cli/ColourMapArg.hpp's normalised name matching. Empty means the
    /// default (grayscale).
    std::string colourMapArg;
    bool invert = false;

    bool requirePixels = false;

    /// --max-width in pixels, if given.
    std::optional<int> maxWidth;
};

/// Outcome of parsing argv: the parsed Options, plus whether parsing
/// succeeded.  On failure (ok == false) a diagnostic has already been
/// written to std::cerr and the caller should exit non-zero.
struct ParseResult
{
    Options options;
    bool ok = true;
};

/// Parse argc/argv into an Options struct using cxxopts.
ParseResult parseArgs(int argc, char** argv);

/// Print usage/help text to stdout.
void printHelp();

/// Print the version string to stdout.
void printVersion();

} // namespace mriv::term
