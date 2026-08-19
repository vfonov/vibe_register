#include "cli/Options.hpp"

#include <iostream>
#include <ostream>
#include <string>
#include <vector>

#include <cxxopts.hpp>

#include "cli/ColourMapArg.hpp"
#include "cli/SliceSelection.hpp"
#include "cli/ViewList.hpp"

namespace mriv::term
{

namespace
{

constexpr const char* kVersion = "mriv 0.1.0";

// Split a comma-separated argument, trimming surrounding spaces. Colour map
// display names contain spaces ("Hot Metal") but never commas, so the split
// is unambiguous. Empty elements are kept so the caller's validation
// rejects them with a name-specific message.
std::vector<std::string> splitOnCommas(const std::string& arg)
{
    std::vector<std::string> parts;
    size_t pos = 0;
    for (;;)
    {
        size_t comma = arg.find(',', pos);
        std::string part = arg.substr(pos, comma == std::string::npos
                                               ? std::string::npos
                                               : comma - pos);
        size_t begin = part.find_first_not_of(" \t");
        size_t end   = part.find_last_not_of(" \t");
        parts.push_back(begin == std::string::npos
                            ? std::string()
                            : part.substr(begin, end - begin + 1));
        if (comma == std::string::npos)
            break;
        pos = comma + 1;
    }
    return parts;
}

cxxopts::Options buildParser()
{
    cxxopts::Options opts("mriv", "Terminal-based slice viewer for MINC2/NIfTI volumes");
    opts.add_options()
        ("h,help", "Show this help message")
        ("version", "Show version")
        ("i,info", "Print volume metadata and exit (no rendering)")
        ("a,axis", "Axis that --slice positions and the keyboard moves: "
            "x|y|z (default: z / axial)",
            cxxopts::value<std::string>()->default_value("z"))
        ("s,slice", "Slice index, percentage, or \"mid\" for the --axis plane "
            "(default: mid); or a per-axis list \"x=<n|p%|mid>,y=...,z=...\" "
            "to position more than one plane at once",
            cxxopts::value<std::string>()->default_value("mid"))
        ("R,range", "Intensity range \"low,high\" for mapping: low maps to the "
            "darkest colour, high to the brightest", cxxopts::value<std::vector<double>>())
        ("auto-window", "Percentile-based auto range (default on)")
        ("v,views", "Planes to show, stacked top to bottom: a comma-separated "
            "subset of x,y,z (default: z,x,y -- all three)",
            cxxopts::value<std::string>()->default_value("z,x,y"))
        ("c,colourmap", "Colour map name, or one name per file separated by "
            "commas (default: Spectral for the first volume, Gray for the "
            "rest). See ColourMap.h.", cxxopts::value<std::string>())
        ("invert", "Invert the colour map")
        ("interactive", "Navigate the volumes with the keyboard (default when "
            "stdout is a terminal)")
        ("no-interactive", "Print one frame and exit, even on a terminal")
        ("max-width", "Cap the rendered image width in pixels", cxxopts::value<int>())
        ("scale", "Integer pixel magnification factor (default: 1)",
            cxxopts::value<int>()->default_value("1"))
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
        if (!parseSliceSpec(sliceArg).has_value())
        {
            err << "mriv: invalid --slice '" << sliceArg
                       << "' (expected an index, a percentage like '50%', or 'mid'; "
                          "or a per-axis list like 'x=10,y=50%,z=mid')\n";
            result.ok = false;
            return result;
        }
        result.options.sliceArg = sliceArg;

        if (parsed.count("range"))
        {
            if (parsed.count("auto-window"))
            {
                err << "mriv: --auto-window cannot be combined with --range\n";
                result.ok = false;
                return result;
            }
            auto vals = parsed["range"].as<std::vector<double>>();
            if (vals.size() != 2)
            {
                err << "mriv: --range requires exactly two comma-separated values, "
                       "\"low,high\"\n";
                result.ok = false;
                return result;
            }
            if (!(vals[0] < vals[1]))
            {
                err << "mriv: --range low (" << vals[0] << ") must be less than high ("
                    << vals[1] << ")\n";
                result.ok = false;
                return result;
            }
            result.options.hasRange  = true;
            result.options.rangeLow  = vals[0];
            result.options.rangeHigh = vals[1];
        }
        result.options.autoWindow = parsed.count("auto-window") > 0;

        std::string viewsArg = parsed["views"].as<std::string>();
        auto views = parseViewList(viewsArg);
        if (!views.has_value())
        {
            err << "mriv: invalid --views '" << viewsArg
                << "' (expected a comma-separated subset of x, y, z)\n";
            result.ok = false;
            return result;
        }
        result.options.views = *views;

        if (parsed.count("colourmap"))
        {
            auto names = splitOnCommas(parsed["colourmap"].as<std::string>());
            for (const auto& name : names)
            {
                if (!resolveColourMapArg(name).has_value())
                {
                    err << "mriv: invalid --colourmap '" << name
                        << "'. Valid names: " << listColourMapNames() << "\n";
                    result.ok = false;
                    return result;
                }
            }
            // One name colours every volume; otherwise the list must name
            // each file exactly once. Any other count is a typo, and
            // reusing or dropping entries would colour a volume by
            // something the user never asked for.
            if (names.size() != 1 && names.size() != result.options.files.size())
            {
                err << "mriv: --colourmap has " << names.size() << " names but "
                    << result.options.files.size() << " file(s) were given; pass "
                       "one name for all of them or one per file\n";
                result.ok = false;
                return result;
            }
            result.options.colourMapArgs = names;
        }

        result.options.invert = parsed.count("invert") > 0;
        result.options.interactive = parsed.count("interactive") > 0;
        result.options.noInteractive = parsed.count("no-interactive") > 0;

        if (parsed.count("max-width"))
            result.options.maxWidth = parsed["max-width"].as<int>();

        result.options.scale = parsed["scale"].as<int>();
        if (result.options.scale < 1)
        {
            err << "mriv: --scale must be >= 1 (got " << result.options.scale << ")\n";
            result.ok = false;
            return result;
        }
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
