#pragma once

#include <string>
#include <vector>

namespace mriv::term
{

/// Parsed command-line state.  Grows as CLI flags are added (see PLAN.md's
/// CLI surface); M1 only needs positional files and --help.
struct Options
{
    std::vector<std::string> files;
    bool help = false;
};

/// Outcome of parsing argv: the parsed Options, plus whether parsing
/// succeeded.  On failure (ok == false) a diagnostic has already been
/// written to std::cerr and the caller should exit non-zero.
struct ParseResult
{
    Options options;
    bool ok = true;
};

/// Parse argc/argv into an Options struct using cxxopts.
ParseResult parseArgs(int argc, char** argv);

/// Print usage/help text to stdout.
void printHelp();

} // namespace mriv::term
