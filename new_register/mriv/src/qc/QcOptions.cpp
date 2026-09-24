#include "qc/QcOptions.hpp"

#include <ostream>
#include <string>
#include <vector>

#include <cxxopts.hpp>

namespace mriv::qc
{

namespace
{

constexpr const char* kVersion = "mriv_qc 0.1.0";

cxxopts::Options buildParser()
{
    cxxopts::Options opts("mriv_qc",
        "Rate a list of PNG/JPG images Pass / Warning / Fail in the terminal.\n"
        "Reads and writes the same CSV files as new_qc.");
    opts.add_options()
        ("h,help", "Show this help message")
        ("version", "Show version")
        ("i,input", "Input CSV with id,visit,picture columns (new_qc's format)",
            cxxopts::value<std::string>())
        ("o,output", "Results CSV (id,visit,picture,QC,notes); resumed if it "
            "already exists", cxxopts::value<std::string>()->default_value("qc_results.csv"))
        ("no-advance", "Stay on the image after giving a verdict")
        ("scale", "Integer pixel magnification (default: fit the window)",
            cxxopts::value<int>())
        ("image", "Image files, or '-' to read a newline-separated list from stdin",
            cxxopts::value<std::vector<std::string>>());
    opts.positional_help("<image>... | -");
    opts.parse_positional({"image"});
    return opts;
}

} // namespace

QcParseResult parseQcArgs(int argc, char** argv, std::ostream& err)
{
    QcParseResult result;
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

        std::vector<std::string> images;
        if (parsed.count("image"))
            images = parsed["image"].as<std::vector<std::string>>();
        if (parsed.count("input"))
            result.options.inputCsv = parsed["input"].as<std::string>();

        bool hasDash = false;
        for (const auto& image : images)
            hasDash = hasDash || image == "-";

        bool hasFiles = images.size() > (hasDash ? 1u : 0u);
        int sources = (result.options.inputCsv.empty() ? 0 : 1) + (hasDash ? 1 : 0)
                      + (hasFiles ? 1 : 0);
        if (sources == 0)
        {
            err << "mriv_qc: no images given (pass image files, -i <csv>, or '-' "
                   "for a list on stdin)\n";
            result.ok = false;
            return result;
        }
        if (sources > 1)
        {
            err << "mriv_qc: give only one input: image files, -i <csv>, or '-'\n";
            result.ok = false;
            return result;
        }

        if (hasDash)
            result.options.readStdin = true;
        else
            result.options.images = images;

        result.options.output = parsed["output"].as<std::string>();
        result.options.autoAdvance = parsed.count("no-advance") == 0;

        if (parsed.count("scale"))
        {
            int scale = parsed["scale"].as<int>();
            if (scale < 1)
            {
                err << "mriv_qc: --scale must be a positive integer, got " << scale << "\n";
                result.ok = false;
                return result;
            }
            result.options.scale = scale;
        }
    }
    catch (const std::exception& e)
    {
        err << "mriv_qc: " << e.what() << "\n";
        result.ok = false;
    }

    return result;
}

void printQcHelp(std::ostream& out)
{
    out << buildParser().help() << "\n"
        << "Keys:\n"
        << "  p / w / f        Pass / Warning / Fail (saved at once, then next image)\n"
        << "  u, Backspace     clear the verdict\n"
        << "  arrows, h/l, j/k, n/b, Space, PgUp/PgDn   previous / next image\n"
        << "  g / G, Home/End  first / last image\n"
        << "  ] / [            next / previous unrated image\n"
        << "  s                save now\n"
        << "  q, Esc           save and quit\n";
}

void printQcVersion(std::ostream& out)
{
    out << kVersion << "\n";
}

} // namespace mriv::qc
