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

void testDefaults()
{
    auto result = parse({"mriv", "a.mnc"});
    assert(result.ok);
    assert(result.options.axis == 'z');
    assert(result.options.sliceArg == "mid");
    assert(!result.options.hasWindowLevel);
    assert(!result.options.autoWindow);
    assert(result.options.colourMapArg.empty());
    assert(!result.options.invert);
    assert(!result.options.requirePixels);
    assert(!result.options.maxWidth.has_value());
    assert(!result.options.info);
    assert(!result.options.version);
}

void testAxisFlag()
{
    assert(parse({"mriv", "-a", "x", "a.mnc"}).options.axis == 'x');
    assert(parse({"mriv", "--axis", "y", "a.mnc"}).options.axis == 'y');
    assert(parse({"mriv", "--axis", "z", "a.mnc"}).options.axis == 'z');
}

void testInvalidAxisRejected()
{
    auto result = parse({"mriv", "--axis", "w", "a.mnc"});
    assert(!result.ok);
}

void testSliceFlag()
{
    assert(parse({"mriv", "-s", "42", "a.mnc"}).options.sliceArg == "42");
    assert(parse({"mriv", "--slice", "50%", "a.mnc"}).options.sliceArg == "50%");
    assert(parse({"mriv", "--slice", "mid", "a.mnc"}).options.sliceArg == "mid");
}

void testInvalidSliceRejected()
{
    auto result = parse({"mriv", "--slice", "not-a-slice", "a.mnc"});
    assert(!result.ok);
}

void testWindowLevelFlags()
{
    auto result = parse({"mriv", "-W", "100", "-L", "50", "a.mnc"});
    assert(result.ok);
    assert(result.options.hasWindowLevel);
    assert(result.options.window == 100.0);
    assert(result.options.level == 50.0);
}

void testWindowWithoutLevelRejected()
{
    // -W without -L (or vice versa) is an incomplete pair -- reject with a
    // clear message rather than silently defaulting level to 0.
    auto result = parse({"mriv", "-W", "100", "a.mnc"});
    assert(!result.ok);
}

void testAutoWindowFlag()
{
    auto result = parse({"mriv", "--auto-window", "a.mnc"});
    assert(result.ok);
    assert(result.options.autoWindow);
}

void testAutoWindowWithWindowLevelRejected()
{
    auto result = parse({"mriv", "--auto-window", "-W", "100", "-L", "50", "a.mnc"});
    assert(!result.ok);
}

void testColourMapFlag()
{
    auto result = parse({"mriv", "-c", "Hot Metal", "a.mnc"});
    assert(result.ok);
    assert(result.options.colourMapArg == "Hot Metal");
}

void testInvalidColourMapRejected()
{
    auto result = parse({"mriv", "--colourmap", "not-a-real-map", "a.mnc"});
    assert(!result.ok);
}

void testInvertFlag()
{
    assert(parse({"mriv", "--invert", "a.mnc"}).options.invert);
}

void testRequirePixelsFlag()
{
    assert(parse({"mriv", "--require-pixels", "a.mnc"}).options.requirePixels);
}

void testMaxWidthFlag()
{
    auto result = parse({"mriv", "--max-width", "800", "a.mnc"});
    assert(result.ok);
    assert(result.options.maxWidth.value() == 800);
}

void testInfoFlag()
{
    assert(parse({"mriv", "-i", "a.mnc"}).options.info);
    assert(parse({"mriv", "--info", "a.mnc"}).options.info);
}

void testVersionFlag()
{
    assert(parse({"mriv", "--version"}).options.version);
}

} // namespace

int main()
{
    testHelpFlag();
    testHelpShortFlag();
    testPositionalFiles();
    testNoArgsIsNotHelp();
    testUnknownFlagFails();
    testDefaults();
    testAxisFlag();
    testInvalidAxisRejected();
    testSliceFlag();
    testInvalidSliceRejected();
    testWindowLevelFlags();
    testWindowWithoutLevelRejected();
    testAutoWindowFlag();
    testAutoWindowWithWindowLevelRejected();
    testColourMapFlag();
    testInvalidColourMapRejected();
    testInvertFlag();
    testRequirePixelsFlag();
    testMaxWidthFlag();
    testInfoFlag();
    testVersionFlag();
    return 0;
}
