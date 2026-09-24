#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace mriv::qc
{

/// Parsed mriv_qc command line. Exactly one input source: positional image
/// files, `-i` CSV (id,visit,picture -- new_qc's input format), or a lone
/// "-" for a newline-separated list on stdin.
struct QcOptions
{
    std::vector<std::string> images;
    std::string inputCsv;
    bool readStdin = false;

    /// Results CSV (id,visit,picture,QC,notes). Resumed if it exists.
    std::string output = "qc_results.csv";

    bool autoAdvance = true;

    /// Integer pixel magnification; 0 = auto-fit (see qc/QcImage.hpp).
    int scale = 0;

    bool help = false;
    bool version = false;
};

struct QcParseResult
{
    QcOptions options;
    bool ok = true;
};

/// Parse argv with cxxopts. On failure `ok` is false and a diagnostic has
/// been written to `err`.
QcParseResult parseQcArgs(int argc, char** argv, std::ostream& err);

void printQcHelp(std::ostream& out);
void printQcVersion(std::ostream& out);

} // namespace mriv::qc
