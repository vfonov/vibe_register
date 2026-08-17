#include "cli/Options.hpp"

#include <iostream>
#include <ostream>

#include <cxxopts.hpp>

#include "cli/ColourMapArg.hpp"
#include "cli/SliceSelection.hpp"

namespace mriv::term
{

namespace
{

constexpr const char* kVersion = "mriv 0.1.0";

cxxopts::Options buildParser()
{
    cxxopts::Options opts("mriv", "Terminal-based slice viewer for MINC2/NIfTI volumes");
    opts.add_options()
        ("h,help", "Show this help message")
        ("version", "Show version")
        ("i,info", "Print volume metadata and exit (no rendering)")
        ("a,axis", "Axis to slice along: x|y|z (default: z / axial)",
            cxxopts::value<std::string>()->default_value("z"))
        ("s,slice", "Slice index, percentage, or \"mid\" (default: mid)",
            cxxopts::value<std::string>()->default_value("mid"))
        ("W,window", "Window width for intensity mapping", cxxopts::value<double>())
        ("L,level", "Window level", cxxopts::value<double>())
        ("auto-window", "Percentile-based auto range (default on)")
        ("c,colourmap", "Colour map name (default: Gray). See ColourMap.h.",
            cxxopts::value<std::string>())
        ("invert", "Invert the colour map")
        ("require-pixels", "Exit non-zero if the terminal has no pixel protocol")
        ("max-width", "Cap the rendered image width in pixels", cxxopts::value<int>())
        ("file", "Volume file(s) to view", cxxopts::value<std::vector<std::string>>());
    opts.positional_help("<file>...");
    opts.parse_positional({"file"});
    return opts;
}

} // namespace

ParseResult parseArgs(int argc, char** argv)
{
    return parseArgs(argc, argv, std::cerr);
}

ParseResult parseArgs(int argc, char** argv, std::ostream& err)
{
    ParseResult result;
    auto opts = buildParser();

    try
    {
        auto parsed = opts.parse(argc, argv);

        if (parsed.count("help"))
        {
            result.options.help = true;
            return result;
        }

        if (parsed.count("version"))
        {
            result.options.version = true;
            return result;
        }

        if (parsed.count("file"))
            result.options.files = parsed["file"].as<std::vector<std::string>>();

        result.options.info = parsed.count("info") > 0;

        std::string axisArg = parsed["axis"].as<std::string>();
        if (axisArg.size() != 1 || (axisArg[0] != 'x' && axisArg[0] != 'y' && axisArg[0] != 'z'))
        {
            err << "mriv: invalid --axis '" << axisArg << "' (expected x, y, or z)\n";
            result.ok = false;
            return result;
        }
        result.options.axis = axisArg[0];

        std::string sliceArg = parsed["slice"].as<std::string>();
        if (!parseSliceArg(sliceArg).has_value())
        {
            err << "mriv: invalid --slice '" << sliceArg
                       << "' (expected an index, a percentage like '50%', or 'mid')\n";
            result.ok = false;
            return result;
        }
        result.options.sliceArg = sliceArg;

        bool hasWindow = parsed.count("window") > 0;
        bool hasLevel  = parsed.count("level") > 0;
        if (hasWindow != hasLevel)
        {
            err << "mriv: --window and --level must be given together\n";
            result.ok = false;
            return result;
        }
        if (hasWindow && hasLevel && parsed.count("auto-window"))
        {
            err << "mriv: --auto-window cannot be combined with --window/--level\n";
            result.ok = false;
            return result;
        }
        if (hasWindow && hasLevel)
        {
            result.options.hasWindowLevel = true;
            result.options.window = parsed["window"].as<double>();
            result.options.level  = parsed["level"].as<double>();
        }
        result.options.autoWindow = parsed.count("auto-window") > 0;

        if (parsed.count("colourmap"))
        {
            std::string colourMapArg = parsed["colourmap"].as<std::string>();
            if (!resolveColourMapArg(colourMapArg).has_value())
            {
                err << "mriv: invalid --colourmap '" << colourMapArg
                           << "'. Valid names: " << listColourMapNames() << "\n";
                result.ok = false;
                return result;
            }
            result.options.colourMapArg = colourMapArg;
        }

        result.options.invert = parsed.count("invert") > 0;
        result.options.requirePixels = parsed.count("require-pixels") > 0;

        if (parsed.count("max-width"))
            result.options.maxWidth = parsed["max-width"].as<int>();
    }
    catch (const std::exception& e)
    {
        err << "mriv: " << e.what() << "\n";
        result.ok = false;
    }

    return result;
}

void printHelp()
{
    printHelp(std::cout);
}

void printHelp(std::ostream& out)
{
    out << buildParser().help() << "\n";
}

void printVersion()
{
    printVersion(std::cout);
}

void printVersion(std::ostream& out)
{
    out << kVersion << "\n";
}

} // namespace mriv::term
