/// mincpik_cli.h — CLI argument parsing for new_mincpik.

#ifndef MINCPIK_CLI_H
#define MINCPIK_CLI_H

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "ColourMap.h"

/// Per-volume display options.  Accumulated while walking argv and flushed
/// to the per-volume vector each time a positional volume file is seen.
struct PerVolOpts
{
    std::optional<ColourMapType> colourMap;
    std::optional<std::array<double, 2>> range;
    bool isLabel = false;
    std::optional<std::string> labelDescFile;
};

/// All parsed command-line arguments.
struct ParsedArgs
{
    // Global flags
    bool help  = false;
    bool debug = false;

    // Output
    std::string outputPath;
    std::optional<int> width;
    int gap = 2;

    // Slice counts
    int nAxial    = 1;
    int nSagittal = 1;
    int nCoronal  = 1;

    // Slice-at world coordinates
    std::string axialAt;
    std::string sagittalAt;
    std::string coronalAt;

    // Layout
    int rows = 1;

    // Config / transform
    std::string configPath;
    std::string tagsPath;
    std::string xfmPath;

    // Per-volume alpha
    std::string alphaStr;

    // Title annotation
    std::string title;
    std::string fgColourStr = "white";
    std::optional<int> fontScale;

    // Volumes and their per-volume options
    std::vector<std::string> volumeFiles;
    std::vector<PerVolOpts>  perVolOpts;
};

/// Print usage / help text to stdout.
void printUsage();

/// Parse all command-line arguments in a single pass.
/// Returns ParsedArgs on success.  On error, prints a message to stderr
/// and returns std::nullopt.
std::optional<ParsedArgs> parseArgs(int argc, char** argv);

#endif // MINCPIK_CLI_H
