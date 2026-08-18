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
    assert(!result.options.hasRange);
    assert(!result.options.autoWindow);
    assert(result.options.colourMapArgs.empty());
    // All three planes by default -- axial, sagittal, coronal, in that order.
    assert(result.options.views.size() == 3);
    assert(result.options.views[0] == 0);
    assert(result.options.views[1] == 1);
    assert(result.options.views[2] == 2);
    assert(!result.options.invert);
    assert(!result.options.requirePixels);
    assert(!result.options.maxWidth.has_value());
    assert(result.options.scale == 1);
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

void testRangeFlag()
{
    auto result = parse({"mriv", "-R", "10,200", "a.mnc"});
    assert(result.ok);
    assert(result.options.hasRange);
    assert(result.options.rangeLow == 10.0);
    assert(result.options.rangeHigh == 200.0);
}

void testRangeLongFlag()
{
    auto result = parse({"mriv", "--range", "-5,5", "a.mnc"});
    assert(result.ok);
    assert(result.options.hasRange);
    assert(result.options.rangeLow == -5.0);
    assert(result.options.rangeHigh == 5.0);
}

void testRangeWrongCountRejected()
{
    auto result = parse({"mriv", "--range", "10,20,30", "a.mnc"});
    assert(!result.ok);
}

void testRangeLowNotBelowHighRejected()
{
    auto result = parse({"mriv", "--range", "200,10", "a.mnc"});
    assert(!result.ok);

    auto equalResult = parse({"mriv", "--range", "50,50", "a.mnc"});
    assert(!equalResult.ok);
}

void testAutoWindowFlag()
{
    auto result = parse({"mriv", "--auto-window", "a.mnc"});
    assert(result.ok);
    assert(result.options.autoWindow);
}

void testAutoWindowWithRangeRejected()
{
    auto result = parse({"mriv", "--auto-window", "--range", "10,200", "a.mnc"});
    assert(!result.ok);
}

void testColourMapFlag()
{
    auto result = parse({"mriv", "-c", "Hot Metal", "a.mnc"});
    assert(result.ok);
    assert(result.options.colourMapArgs.size() == 1);
    assert(result.options.colourMapArgs[0] == "Hot Metal");
}

/// One map per file, in argument order. Colour-map display names contain
/// spaces but never commas, so splitting on ',' is unambiguous.
void testColourMapListPerFile()
{
    auto result = parse({"mriv", "-c", "Spectral,Hot Metal", "a.mnc", "b.mnc"});
    assert(result.ok);
    assert(result.options.colourMapArgs.size() == 2);
    assert(result.options.colourMapArgs[0] == "Spectral");
    assert(result.options.colourMapArgs[1] == "Hot Metal");
}

void testInvalidColourMapInListRejected()
{
    assert(!parse({"mriv", "-c", "Spectral,not-a-real-map", "a.mnc", "b.mnc"}).ok);
}

/// A list whose length is neither 1 nor the file count is a typo, not an
/// instruction: silently reusing or dropping entries would leave a volume
/// coloured by something the user never named.
void testColourMapCountMismatchRejected()
{
    assert(!parse({"mriv", "-c", "Spectral,Gray", "a.mnc"}).ok);
    assert(!parse({"mriv", "-c", "Spectral,Gray", "a.mnc", "b.mnc", "c.mnc"}).ok);
}

void testViewsFlag()
{
    auto result = parse({"mriv", "--views", "y,z", "a.mnc"});
    assert(result.ok);
    assert(result.options.views.size() == 2);
    assert(result.options.views[0] == 2);
    assert(result.options.views[1] == 0);
}

void testSingleViewFlag()
{
    auto result = parse({"mriv", "--views", "x", "a.mnc"});
    assert(result.ok);
    assert(result.options.views.size() == 1);
    assert(result.options.views[0] == 1);
}

void testInvalidViewsRejected()
{
    assert(!parse({"mriv", "--views", "w", "a.mnc"}).ok);
    assert(!parse({"mriv", "--views", "", "a.mnc"}).ok);
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

void testScaleFlag()
{
    auto result = parse({"mriv", "--scale", "4", "a.mnc"});
    assert(result.ok);
    assert(result.options.scale == 4);
}

void testScaleDefaultIsOne()
{
    auto result = parse({"mriv", "a.mnc"});
    assert(result.ok);
    assert(result.options.scale == 1);
}

void testScaleZeroRejected()
{
    auto result = parse({"mriv", "--scale", "0", "a.mnc"});
    assert(!result.ok);
}

void testScaleNegativeRejected()
{
    auto result = parse({"mriv", "--scale", "-2", "a.mnc"});
    assert(!result.ok);
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
    testRangeFlag();
    testRangeLongFlag();
    testRangeWrongCountRejected();
    testRangeLowNotBelowHighRejected();
    testAutoWindowFlag();
    testAutoWindowWithRangeRejected();
    testColourMapFlag();
    testColourMapListPerFile();
    testInvalidColourMapRejected();
    testInvalidColourMapInListRejected();
    testColourMapCountMismatchRejected();
    testViewsFlag();
    testSingleViewFlag();
    testInvalidViewsRejected();
    testInvertFlag();
    testRequirePixelsFlag();
    testMaxWidthFlag();
    testScaleFlag();
    testScaleDefaultIsOne();
    testScaleZeroRejected();
    testScaleNegativeRejected();
    testInfoFlag();
    testVersionFlag();
    return 0;
}
