/// mincpik_cli.cpp — CLI argument parsing for new_mincpik.

#include "mincpik_cli.h"

#include <iostream>
#include <string>
#include <string_view>

#include "ColourMap.h"
#include "mosaic.h"

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

void printUsage()
{
    std::cout <<
        "Usage: new_mincpik [OPTIONS] [VOLUMES...]\n"
        "\n"
        "Headless mosaic image generator for MINC2 volumes.\n"
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
        "      --qrange <q0,q1>  Quantile range [0,1] for next volume\n"
        "  -l, --label          Mark next volume as label volume\n"
        "  -L, --labels <file>  Label description file for next volume\n"
        "\n"
        "Slice selection:\n"
        "      --axial <N>      Number of axial slices (default: 1)\n"
        "      --sagittal <N>   Number of sagittal slices (default: 1)\n"
        "      --coronal <N>    Number of coronal slices (default: 1)\n"
        "      --axial-at <Z,...>    World Z coordinates (comma-separated)\n"
        "      --sagittal-at <X,...> World X coordinates (comma-separated)\n"
        "      --coronal-at <Y,...>  World Y coordinates (comma-separated)\n"
        "\n"
        "Layout:\n"
        "      --rows <N>       Arrange each plane's slices across N rows\n"
        "\n"
        "Output:\n"
        "  -o, --output <path>  Output PNG path (required)\n"
        "      --width <px>     Scale output to this width in pixels\n"
        "      --gap <px>       Cell gap in pixels (default: 2)\n"
        "\n"
        "Annotation:\n"
        "      --title <text>   Title text rendered above the mosaic\n"
        "      --fg <colour>    Foreground colour for title/bar (default: white)\n"
        "                       Hex: #RRGGBB, #RGB, RRGGBB, RGB\n"
        "                       Named: white, black, red, green, blue,\n"
        "                              yellow, cyan, magenta, gray, orange\n"
        "      --font-scale <N> Integer scale for 12px font (default: auto)\n"
        "      --bar <side>     Show colour bar: 'right' or 'bottom'\n"
        "                       Continuous gradient with min/mid/max labels,\n"
        "                       or discrete legend for label volumes\n"
        "\n"
        "Transform:\n"
        "  -t, --tags <file>    Load .tag file for registration\n"
        "      --xfm <file>     Load .xfm linear transform file\n"
        "\n"
        "Misc:\n"
        "  -c, --config <file>  Load config.json\n"
        "      --alpha <a,...>  Per-volume overlay alpha (comma-separated)\n"
        "  -d, --debug          Enable debug output\n"
        "  -h, --help           Show this help message\n"
        "\n";

    std::cout << "Available colour maps (for --lut):\n";
    for (int cm = 0; cm < colourMapCount(); ++cm)
        std::cout << "  " << colourMapName(static_cast<ColourMapType>(cm)) << "\n";

    std::cout << "\nExamples:\n"
              << "  new_mincpik --gray vol1.mnc -r vol2.mnc --coronal 5 -o mosaic.png\n"
              << "  new_mincpik vol.mnc --coronal 12 --rows 3 -o mosaic.png\n";
}

std::optional<ParsedArgs> parseArgs(int argc, char** argv)
{
    ParsedArgs args;

    // Pending per-volume state, flushed when a positional arg is seen.
    std::optional<ColourMapType> pendingLut;
    bool pendingLabel = false;
    std::optional<std::string> pendingLabelDesc;
    std::optional<double> pendingMin, pendingMax;
    std::optional<double> pendingQMin, pendingQMax;

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg = argv[i];

        // -- Boolean flags (no value) --

        if (arg == "-h" || arg == "--help")
        {
            args.help = true;
            continue;
        }
        if (arg == "-d" || arg == "--debug")
        {
            args.debug = true;
            continue;
        }

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
            auto vals = parseDoubleList(argv[i]);
            if (vals.size() >= 2)
            {
                pendingMin = vals[0];
                pendingMax = vals[1];
            }
            continue;
        }

        if (arg == "--qrange")
        {
            ++i;
            if (!requireValue(i, argc, "--qrange"))
                return std::nullopt;
            auto vals = parseDoubleList(argv[i]);
            if (vals.size() >= 2)
            {
                if (vals[0] < 0.0 || vals[0] > 1.0 || vals[1] < 0.0 || vals[1] > 1.0)
                {
                    std::cerr << "Error: --qrange values must be in [0.0, 1.0].\n";
                    return std::nullopt;
                }
                if (vals[0] >= vals[1])
                {
                    std::cerr << "Error: --qrange qmin must be less than qmax.\n";
                    return std::nullopt;
                }
                pendingQMin = vals[0];
                pendingQMax = vals[1];
            }
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

        if (arg == "-o" || arg == "--output")
        {
            ++i;
            if (!requireValue(i, argc, "--output"))
                return std::nullopt;
            args.outputPath = argv[i];
            continue;
        }

        if (arg == "--width")
        {
            ++i;
            if (!requireValue(i, argc, "--width"))
                return std::nullopt;
            args.width = std::stoi(argv[i]);
            continue;
        }

        if (arg == "--gap")
        {
            ++i;
            if (!requireValue(i, argc, "--gap"))
                return std::nullopt;
            args.gap = std::stoi(argv[i]);
            continue;
        }

        if (arg == "--axial")
        {
            ++i;
            if (!requireValue(i, argc, "--axial"))
                return std::nullopt;
            args.nAxial = std::stoi(argv[i]);
            continue;
        }

        if (arg == "--sagittal")
        {
            ++i;
            if (!requireValue(i, argc, "--sagittal"))
                return std::nullopt;
            args.nSagittal = std::stoi(argv[i]);
            continue;
        }

        if (arg == "--coronal")
        {
            ++i;
            if (!requireValue(i, argc, "--coronal"))
                return std::nullopt;
            args.nCoronal = std::stoi(argv[i]);
            continue;
        }

        if (arg == "--axial-at")
        {
            ++i;
            if (!requireValue(i, argc, "--axial-at"))
                return std::nullopt;
            args.axialAt = argv[i];
            continue;
        }

        if (arg == "--sagittal-at")
        {
            ++i;
            if (!requireValue(i, argc, "--sagittal-at"))
                return std::nullopt;
            args.sagittalAt = argv[i];
            continue;
        }

        if (arg == "--coronal-at")
        {
            ++i;
            if (!requireValue(i, argc, "--coronal-at"))
                return std::nullopt;
            args.coronalAt = argv[i];
            continue;
        }

        if (arg == "--rows")
        {
            ++i;
            if (!requireValue(i, argc, "--rows"))
                return std::nullopt;
            args.rows = std::stoi(argv[i]);
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

        if (arg == "--xfm")
        {
            ++i;
            if (!requireValue(i, argc, "--xfm"))
                return std::nullopt;
            args.xfmPath = argv[i];
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

        if (arg == "--alpha")
        {
            ++i;
            if (!requireValue(i, argc, "--alpha"))
                return std::nullopt;
            args.alphaStr = argv[i];
            continue;
        }

        if (arg == "--title")
        {
            ++i;
            if (!requireValue(i, argc, "--title"))
                return std::nullopt;
            args.title = argv[i];
            continue;
        }

        if (arg == "--fg")
        {
            ++i;
            if (!requireValue(i, argc, "--fg"))
                return std::nullopt;
            args.fgColourStr = argv[i];
            continue;
        }

        if (arg == "--font-scale")
        {
            ++i;
            if (!requireValue(i, argc, "--font-scale"))
                return std::nullopt;
            args.fontScale = std::stoi(argv[i]);
            continue;
        }

        if (arg == "--bar")
        {
            ++i;
            if (!requireValue(i, argc, "--bar"))
                return std::nullopt;
            std::string_view side = argv[i];
            if (side == "right")
                args.barSide = BarSide::Right;
            else if (side == "bottom")
                args.barSide = BarSide::Bottom;
            else
            {
                std::cerr << "Error: --bar must be 'right' or 'bottom', got '"
                          << side << "'.\n";
                return std::nullopt;
            }
            continue;
        }

        // -- Unknown flag --

        if (arg.size() > 1 && arg[0] == '-')
        {
            std::cerr << "Error: unknown option: " << arg << "\n"
                      << "Run 'new_mincpik --help' for usage.\n";
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
        if (pendingQMin && pendingQMax)
        {
            pvo.qrange = std::array<double, 2>{*pendingQMin, *pendingQMax};
            pendingQMin.reset();
            pendingQMax.reset();
        }

        args.volumeFiles.push_back(std::string(arg));
        args.perVolOpts.push_back(std::move(pvo));
    }

    return args;
}
