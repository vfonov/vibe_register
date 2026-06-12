#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "ColourMap.h"

// ---------------------------------------------------------------------------
// Command-line argument parsing for new_register.
//
// Shared between the GLFW entry point (main.cpp) and the native macOS entry
// point (main_macos.mm) so the CLI behaves identically on every platform.
// ---------------------------------------------------------------------------

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
    bool help  = false;
    bool debug = false;
    bool test  = false;

    std::string configPath;
    std::string backendName;
    std::string tagsPath;
    std::string qcInputPath;
    std::string qcOutputPath;
    bool qcSingleMode = false;

    bool syncAll    = false;
    bool syncCursor = false;
    bool syncZoom   = false;
    bool syncPan    = false;

    std::optional<float> scaleFactor;

    std::vector<std::string> volumeFiles;
    std::vector<PerVolOpts>  perVolOpts;
};

/// Print usage / help text to stdout.
void printUsage();

/// Parse all command-line arguments in a single pass.
/// Returns ParsedArgs on success.  On error, prints a message to stderr
/// and returns std::nullopt.
std::optional<ParsedArgs> parseArgs(int argc, char** argv);
