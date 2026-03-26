/// mincpik_cli.h — CLI argument parsing for new_mincpik.

#ifndef MINCPIK_CLI_H
#define MINCPIK_CLI_H

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "ColourMap.h"

/// Which side of the mosaic to place the colour bar, or None to omit it.
enum class BarSide { None, Right, Bottom };

/// Per-volume display options.  Accumulated while walking argv and flushed
/// to the per-volume vector each time a positional volume file is seen.
struct PerVolOpts
{
    std::optional<ColourMapType> colourMap;
    std::optional<std::array<double, 2>> range;
    std::optional<std::array<double, 2>> qrange;  // quantile pair [0,1]
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
    std::optional<double> scale;   // uniform scale factor (0.5 = half size, 2.0 = double)
    std::optional<std::array<int,6>> crop;  // [x1,x2,y1,y2,z1,z2] voxels to remove per edge
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

    // Colour bar
    BarSide barSide = BarSide::None;

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
