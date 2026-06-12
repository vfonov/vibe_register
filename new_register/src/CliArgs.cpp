#include "CliArgs.h"

#include <iostream>
#include <string_view>

// ---------------------------------------------------------------------------
// CLI argument parsing (replaces cxxopts).  Shared by main.cpp (GLFW) and
// main_macos.mm (native Cocoa).
// ---------------------------------------------------------------------------

void printUsage()
{
    std::cout <<
        "Usage: new_register [OPTIONS] [VOLUMES...]\n"
        "\n"
        "Medical imaging volume viewer (MINC and NIfTI formats).\n"
        "\n"
        "Supported formats: .mnc, .mnc.gz, .nii, .nii.gz\n"
        "\n"
        "Volume display options (apply to the NEXT volume file):\n"
        "  -G, --gray           GrayScale colour map\n"
        "  -H, --hot            HotMetal colour map\n"
        "  -S, --spectral       Spectral colour map\n"
        "  -r, --red            Red colour map\n"
        "  -g, --green          Green colour map\n"
        "  -b, --blue           Blue colour map\n"
        "      --lut <name>     Named colour map (see list below)\n"
        "      --range <min,max>  Value range for next volume\n"
        "  -l, --label          Mark next volume as label volume\n"
        "  -L, --labels <file>  Label description file for next volume\n"
        "\n"
        "General:\n"
        "  -c, --config <path>  Load config from <path>\n"
        "  -B, --backend <name> Graphics backend: auto, vulkan, opengl2\n"
        "  -t, --tags <file>    Load combined two-volume .tag file\n"
        "  -d, --debug          Enable debug output\n"
        "  -h, --help           Show this help message\n"
        "      --test           Launch with a generated test volume\n"
        "      --scale <factor> Override screen content scale (HiDPI)\n"
        "\n"
        "QC mode:\n"
        "      --qc <csv>       Enable QC mode with input CSV (per-column verdicts)\n"
        "      --qc1 <csv>      Enable QC mode with single verdict per row\n"
        "      --qc-output <csv>  Output CSV for QC verdicts (required with --qc/--qc1)\n"
        "\n"
        "Synchronization:\n"
        "      --sync           Synchronize all (cursor, zoom, pan)\n"
        "      --sync-cursor    Synchronize cursor position across volumes\n"
        "      --sync-zoom      Synchronize zoom level across volumes\n"
        "      --sync-pan       Synchronize pan position across volumes\n"
        "\n"
        "Backends:\n"
        "  vulkan   Vulkan (default where available, best performance)\n"
        "  opengl2  OpenGL 2.1 (legacy, works over SSH/X11)\n"
        "  metal    Metal (native macOS)\n"
        "  auto     Auto-detect best available (default)\n"
        "\n";

    std::cout << "Available colour maps (for --lut):\n";
    for (int cm = 0; cm < colourMapCount(); ++cm)
        std::cout << "  " << colourMapName(static_cast<ColourMapType>(cm)) << "\n";

    std::cout << "\nLUT and range flags apply to the next volume file on the command line.\n"
              << "Example: new_register --gray --range 0,100 vol1.mnc -r vol2.mnc\n";
}

/// Require a value argument after a flag, printing an error and returning
/// false if we've run past the end of argv.
static bool requireValue(int i, int argc, const char* flag)
{
    if (i >= argc)
    {
        std::cerr << "Error: " << flag << " requires a value.\n";
        return false;
    }
    return true;
}

std::optional<ParsedArgs> parseArgs(int argc, char** argv)
{
    ParsedArgs args;

    // Pending per-volume state, flushed when a positional arg is seen.
    std::optional<ColourMapType> pendingLut;
    bool pendingLabel = false;
    std::optional<std::string> pendingLabelDesc;
    std::optional<double> pendingMin, pendingMax;

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg = argv[i];

        // -- Boolean flags (no value) --

        if (arg == "-h" || arg == "--help")    { args.help = true;  continue; }
        if (arg == "-d" || arg == "--debug")   { args.debug = true; continue; }
        if (arg == "--test")                   { args.test = true;  continue; }

        if (arg == "--sync")        { args.syncAll = true;    continue; }
        if (arg == "--sync-cursor") { args.syncCursor = true; continue; }
        if (arg == "--sync-zoom")   { args.syncZoom = true;   continue; }
        if (arg == "--sync-pan")    { args.syncPan = true;    continue; }

        // -- Per-volume colour map shorthands (no value) --

        if (arg == "-G" || arg == "--gray")     { pendingLut = ColourMapType::GrayScale; continue; }
        if (arg == "-H" || arg == "--hot")      { pendingLut = ColourMapType::HotMetal;  continue; }
        if (arg == "-S" || arg == "--spectral") { pendingLut = ColourMapType::Spectral;  continue; }
        if (arg == "-r" || arg == "--red")      { pendingLut = ColourMapType::Red;       continue; }
        if (arg == "-g" || arg == "--green")    { pendingLut = ColourMapType::Green;     continue; }
        if (arg == "-b" || arg == "--blue")     { pendingLut = ColourMapType::Blue;      continue; }
        if (arg == "-l" || arg == "--label")    { pendingLabel = true;                   continue; }

        // -- Valued flags (consume next arg) --

        if (arg == "--lut")
        {
            ++i;
            if (!requireValue(i, argc, "--lut"))
                return std::nullopt;
            auto cmOpt = colourMapByName(argv[i]);
            if (cmOpt)
            {
                pendingLut = *cmOpt;
            }
            else
            {
                std::cerr << "Unknown colour map: " << argv[i] << "\n"
                          << "Available maps:";
                for (int cm = 0; cm < colourMapCount(); ++cm)
                    std::cerr << " " << colourMapName(static_cast<ColourMapType>(cm));
                std::cerr << "\n";
                return std::nullopt;
            }
            continue;
        }

        if (arg == "--range")
        {
            ++i;
            if (!requireValue(i, argc, "--range"))
                return std::nullopt;
            std::string rangeStr = argv[i];
            auto commaPos = rangeStr.find(',');
            if (commaPos == std::string::npos)
            {
                std::cerr << "Error: --range must be in format <min>,<max> (e.g., --range 0,100)\n";
                return std::nullopt;
            }
            pendingMin = std::stod(rangeStr.substr(0, commaPos));
            pendingMax = std::stod(rangeStr.substr(commaPos + 1));
            continue;
        }

        if (arg == "-L" || arg == "--labels")
        {
            ++i;
            if (!requireValue(i, argc, "--labels"))
                return std::nullopt;
            pendingLabelDesc = argv[i];
            continue;
        }

        if (arg == "-c" || arg == "--config")
        {
            ++i;
            if (!requireValue(i, argc, "--config"))
                return std::nullopt;
            args.configPath = argv[i];
            continue;
        }

        if (arg == "-B" || arg == "--backend")
        {
            ++i;
            if (!requireValue(i, argc, "--backend"))
                return std::nullopt;
            args.backendName = argv[i];
            continue;
        }

        if (arg == "-t" || arg == "--tags")
        {
            ++i;
            if (!requireValue(i, argc, "--tags"))
                return std::nullopt;
            args.tagsPath = argv[i];
            continue;
        }

        if (arg == "--qc")
        {
            ++i;
            if (!requireValue(i, argc, "--qc"))
                return std::nullopt;
            args.qcInputPath = argv[i];
            continue;
        }

        if (arg == "--qc1")
        {
            ++i;
            if (!requireValue(i, argc, "--qc1"))
                return std::nullopt;
            args.qcInputPath = argv[i];
            args.qcSingleMode = true;
            continue;
        }

        if (arg == "--qc-output")
        {
            ++i;
            if (!requireValue(i, argc, "--qc-output"))
                return std::nullopt;
            args.qcOutputPath = argv[i];
            continue;
        }

        if (arg == "--scale")
        {
            ++i;
            if (!requireValue(i, argc, "--scale"))
                return std::nullopt;
            args.scaleFactor = std::stof(argv[i]);
            continue;
        }

        // -- Unknown flag --

        if (arg.size() > 1 && arg[0] == '-')
        {
            std::cerr << "Error: unknown option: " << arg << "\n"
                      << "Run 'new_register --help' for usage.\n";
            return std::nullopt;
        }

        // -- Positional: volume file --
        // Flush any pending per-volume options.

        PerVolOpts pvo;
        if (pendingLut)
        {
            pvo.colourMap = *pendingLut;
            pendingLut.reset();
        }
        if (pendingLabel)
        {
            pvo.isLabel = true;
            pendingLabel = false;
        }
        if (pendingLabelDesc)
        {
            pvo.labelDescFile = *pendingLabelDesc;
            pendingLabelDesc.reset();
        }
        if (pendingMin && pendingMax)
        {
            pvo.range = std::array<double, 2>{*pendingMin, *pendingMax};
            pendingMin.reset();
            pendingMax.reset();
        }

        args.volumeFiles.push_back(std::string(arg));
        args.perVolOpts.push_back(std::move(pvo));
    }

    // Warn about unused pending per-volume state
    if (pendingLut.has_value())
        std::cerr << "Warning: LUT flag at end of arguments has no volume to apply to\n";
    if (pendingLabel)
        std::cerr << "Warning: --label flag at end of arguments has no volume to apply to\n";
    if (pendingLabelDesc)
        std::cerr << "Warning: --labels flag at end of arguments has no volume to apply to\n";
    if (pendingMin || pendingMax)
        std::cerr << "Warning: --range "
                  << (pendingMin ? std::to_string(*pendingMin) : "?")
                  << "," << (pendingMax ? std::to_string(*pendingMax) : "?")
                  << " at end of arguments has no volume to apply to\n";

    return args;
}
