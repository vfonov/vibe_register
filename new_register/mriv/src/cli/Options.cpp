#include "cli/Options.hpp"

#include <iostream>

#include <cxxopts.hpp>

namespace mriv::term
{

namespace
{

cxxopts::Options buildParser()
{
    cxxopts::Options opts("mriv", "Terminal-based slice viewer for MINC2/NIfTI volumes");
    opts.add_options()
        ("h,help", "Show this help message")
        ("file", "Volume file(s) to view", cxxopts::value<std::vector<std::string>>());
    opts.positional_help("<file>...");
    opts.parse_positional({"file"});
    return opts;
}

} // namespace

ParseResult parseArgs(int argc, char** argv)
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

        if (parsed.count("file"))
            result.options.files = parsed["file"].as<std::vector<std::string>>();
    }
    catch (const std::exception& e)
    {
        std::cerr << "mriv: " << e.what() << "\n";
        result.ok = false;
    }

    return result;
}

void printHelp()
{
    std::cout << buildParser().help() << "\n";
}

} // namespace mriv::term
