#pragma once

#include <iosfwd>
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

    /// Raw --slice argument: either "n", "p%", or "mid" (applies to
    /// `axis`), or a per-axis list like "x=10,y=50%,z=mid" (any subset of
    /// x/y/z, positioned independently of `axis`); validated by
    /// parseArgs() but resolved against a volume's dimensions later
    /// (see cli/SliceSelection.hpp).
    std::string sliceArg = "mid";

    /// Set when --range was given, overriding --auto-window (the
    /// default). low maps to the darkest colour, high to the brightest --
    /// directly the parent's valueMin/valueMax model, no conversion.
    bool hasRange = false;
    double rangeLow = 0.0;
    double rangeHigh = 0.0;

    /// Explicit --auto-window flag. Auto-window is already the default
    /// when --range isn't given; this flag exists so scripts can say so
    /// explicitly (PLAN.md's CLI surface). It is an error to combine it
    /// with --range.
    bool autoWindow = false;

    /// Raw --colourmap arguments, split on commas and each already
    /// validated by parseArgs() against cli/ColourMapArg.hpp's normalised
    /// name matching. Empty means the built-in defaults (Spectral for the
    /// first volume, grayscale for the rest). One entry applies to every
    /// volume; otherwise there is exactly one entry per file -- parseArgs()
    /// rejects any other count.
    std::vector<std::string> colourMapArgs;
    bool invert = false;

    /// --views, resolved to the parent's viewIndex convention in the order
    /// given (see cli/ViewList.hpp). Defaults to all three planes: axial,
    /// sagittal, coronal. Each entry becomes one row of the display grid.
    /// Distinct from `axis`, which picks the *active* axis that interactive
    /// slice navigation moves.
    std::vector<int> views{0, 1, 2};

    /// --interactive / --no-interactive. Interactive mode is otherwise
    /// auto-detected (a TTY plus a single file); these force the question
    /// either way. See cli/InteractiveDecision.hpp for the rules.
    bool interactive = false;
    bool noInteractive = false;

    /// --max-width in pixels, if given.
    std::optional<int> maxWidth;

    /// --scale: integer pixel-magnification factor applied after
    /// resampling to the terminal's display box (default: 1, no
    /// magnification). Each display pixel becomes an NxN block via
    /// nearest-neighbour replication -- see render/Resample.hpp. Must be
    /// >= 1; validated by parseArgs().
    int scale = 1;
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

/// Parse argc/argv, writing diagnostics to `err` on failure.
ParseResult parseArgs(int argc, char** argv, std::ostream& err);

/// Print usage/help text to stdout.
void printHelp();

/// Print usage/help text to the supplied stream.
void printHelp(std::ostream& out);

/// Print the version string to stdout.
void printVersion();

/// Print the version string to the supplied stream.
void printVersion(std::ostream& out);

} // namespace mriv::term
