/// test_options.cpp — CLI argument parsing (mriv::cli::parseArgs).

#include <cassert>
#include <cstring>
#include <vector>

#include "cli/Options.hpp"

using mriv::term::parseArgs;

namespace
{

// Build a non-const char* argv array from string literals, the way a real
// argv arrives, and hand it to parseArgs.
mriv::term::ParseResult parse(std::vector<const char*> args)
{
    std::vector<char*> argv;
    for (auto* a : args)
        argv.push_back(const_cast<char*>(a));
    return parseArgs(static_cast<int>(argv.size()), argv.data());
}

void testHelpFlag()
{
    auto result = parse({"mriv", "--help"});
    assert(result.ok);
    assert(result.options.help);
}

void testHelpShortFlag()
{
    auto result = parse({"mriv", "-h"});
    assert(result.ok);
    assert(result.options.help);
}

void testPositionalFiles()
{
    auto result = parse({"mriv", "a.mnc", "b.nii.gz"});
    assert(result.ok);
    assert(!result.options.help);
    assert(result.options.files.size() == 2);
    assert(result.options.files[0] == "a.mnc");
    assert(result.options.files[1] == "b.nii.gz");
}

void testNoArgsIsNotHelp()
{
    auto result = parse({"mriv"});
    assert(result.ok);
    assert(!result.options.help);
    assert(result.options.files.empty());
}

void testUnknownFlagFails()
{
    auto result = parse({"mriv", "--not-a-real-flag"});
    assert(!result.ok);
}

} // namespace

int main()
{
    testHelpFlag();
    testHelpShortFlag();
    testPositionalFiles();
    testNoArgsIsNotHelp();
    testUnknownFlagFails();
    return 0;
}
