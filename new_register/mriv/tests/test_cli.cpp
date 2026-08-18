/// test_cli.cpp — Layer C CLI integration tests.
///
/// Invokes mriv::term::run() with injected streams and real-looking
/// arguments. Verifies exit codes, error messages, and (in
/// MRIV_TEST_RENDER=1 mode) the full pipeline from argv to escape bytes.

#include <cassert>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "cli/Run.hpp"
#include "escapes.hpp"

using namespace mriv::term;
using namespace mriv::term::test;

namespace
{

struct Args
{
    std::vector<std::string> storage;
    std::vector<char*> argv;

    explicit Args(std::initializer_list<std::string> args)
        : storage(args)
    {
        for (auto& a : storage)
            argv.push_back(a.data());
    }

    int argc() const { return static_cast<int>(argv.size()); }
    char** data() { return argv.data(); }
};

void testHelpReturnsZeroAndUsage()
{
    std::istringstream in;
    std::ostringstream out, err;
    int rc = run(Args{"mriv", "--help"}.argc(), Args{"mriv", "--help"}.data(), in, out, err);
    assert(rc == 0);
    assert(out.str().find("Usage:") != std::string::npos);
    assert(err.str().empty());
}

void testVersionReturnsZero()
{
    std::istringstream in;
    std::ostringstream out, err;
    int rc = run(Args{"mriv", "--version"}.argc(), Args{"mriv", "--version"}.data(), in, out, err);
    assert(rc == 0);
    assert(out.str().find("mriv") != std::string::npos);
}

void testUnknownFlagFails()
{
    std::istringstream in;
    std::ostringstream out, err;
    int rc = run(Args{"mriv", "--not-a-thing"}.argc(),
                 Args{"mriv", "--not-a-thing"}.data(), in, out, err);
    assert(rc != 0);
    assert(!err.str().empty());
}

void testNoFilesFails()
{
    std::istringstream in;
    std::ostringstream out, err;
    int rc = run(Args{"mriv"}.argc(), Args{"mriv"}.data(), in, out, err);
    assert(rc != 0);
    assert(err.str().find("no input files") != std::string::npos);
}

void testMissingFileFails()
{
    // Forces the test-mode terminal so this exercises what it means to --
    // file handling -- rather than the host terminal's pixel support. The
    // pixel-protocol check runs before any load, so without this the result
    // depended on whether the developer's terminal happened to support
    // pixels: no-pixel hosts never reached the load at all.
    setenv("MRIV_TEST_RENDER", "1", 1);

    std::istringstream in;
    std::ostringstream out, err;
    int rc = run(Args{"mriv", "/does/not/exist.mnc"}.argc(),
                 Args{"mriv", "/does/not/exist.mnc"}.data(), in, out, err);
    assert(rc != 0);
    assert(err.str().find("failed to load") != std::string::npos);

    unsetenv("MRIV_TEST_RENDER");
}

void testInfoProducesNoImageEvents(const char* fixturePath)
{
    std::istringstream in;
    std::ostringstream out, err;
    int rc = run(Args{"mriv", "--info", fixturePath}.argc(),
                 Args{"mriv", "--info", fixturePath}.data(), in, out, err);
    assert(rc == 0);
    assert(out.str().find("dimensions:") != std::string::npos);

    auto events = parseEscapeStream(out.str());
    for (const auto& e : events)
        assert(e.kind != EventKind::KittyGraphics);
}

void testRenderPipelineProducesKittyImage(const char* fixturePath)
{
    // Force the test-mode render path (Kitty protocol) so this test is
    // deterministic regardless of the host terminal.
    setenv("MRIV_TEST_RENDER", "1", 1);

    std::istringstream in;
    std::ostringstream out, err;
    int rc = run(Args{"mriv", fixturePath}.argc(),
                 Args{"mriv", fixturePath}.data(), in, out, err);
    assert(rc == 0);

    auto events = parseEscapeStream(out.str());
    assert(events.size() == 1);
    assert(events[0].kind == EventKind::KittyGraphics);
    assert(events[0].params.count("s") == 1);
    assert(events[0].params.count("v") == 1);
    assert(!events[0].payload.empty());

    unsetenv("MRIV_TEST_RENDER");
}

void testMaxWidthCap(const char* fixturePath)
{
    setenv("MRIV_TEST_RENDER", "1", 1);

    std::istringstream in;
    std::ostringstream out, err;
    int rc = run(Args{"mriv", "--max-width", "64", fixturePath}.argc(),
                 Args{"mriv", "--max-width", "64", fixturePath}.data(), in, out, err);
    assert(rc == 0);

    auto events = parseEscapeStream(out.str());
    assert(events.size() == 1);
    assert(events[0].kind == EventKind::KittyGraphics);
    assert(std::stoi(events[0].params.at("s")) <= 64);

    unsetenv("MRIV_TEST_RENDER");
}

void testRangeFlagChangesImage(const char* fixturePath)
{
    // -R/--range replaced -W/-L: low maps to the darkest colour, high to
    // the brightest, straight into valueMin/valueMax -- no window/level
    // arithmetic in between. sq1.mnc's values are exactly {0, 1} (see
    // tests/dump_vol output), so a range straddling both ends without
    // touching them ([-1, 2]) actually changes the mapped shade -- unlike
    // e.g. "0,1", which clamps identically to the default auto-window
    // range and would make this test pass for the wrong reason.
    setenv("MRIV_TEST_RENDER", "1", 1);

    std::istringstream in1;
    std::ostringstream out1, err1;
    int rc1 = run(Args{"mriv", fixturePath}.argc(), Args{"mriv", fixturePath}.data(), in1, out1, err1);
    assert(rc1 == 0);

    std::istringstream in2;
    std::ostringstream out2, err2;
    int rc2 = run(Args{"mriv", "--range", "-1,2", fixturePath}.argc(),
                 Args{"mriv", "--range", "-1,2", fixturePath}.data(), in2, out2, err2);
    assert(rc2 == 0);

    assert(out1.str() != out2.str());

    unsetenv("MRIV_TEST_RENDER");
}

void testScaleFlagMagnifiesImage(const char* fixturePath)
{
    // --scale is an integer pixel-magnification factor. In test mode the
    // synthetic terminal box (4096x4096, see Terminal::pixelGeometry())
    // is far bigger than sq1.mnc's native slice size, so the pre-scale
    // fit stays at native resolution both with and without --scale, and
    // --scale 3 must triple the blitted width and height exactly.
    setenv("MRIV_TEST_RENDER", "1", 1);

    std::istringstream in1;
    std::ostringstream out1, err1;
    int rc1 = run(Args{"mriv", fixturePath}.argc(), Args{"mriv", fixturePath}.data(), in1, out1, err1);
    assert(rc1 == 0);
    auto baseEvents = parseEscapeStream(out1.str());
    assert(baseEvents.size() == 1);

    std::istringstream in2;
    std::ostringstream out2, err2;
    int rc2 = run(Args{"mriv", "--scale", "3", fixturePath}.argc(),
                 Args{"mriv", "--scale", "3", fixturePath}.data(), in2, out2, err2);
    assert(rc2 == 0);
    auto scaledEvents = parseEscapeStream(out2.str());
    assert(scaledEvents.size() == 1);

    int baseW = std::stoi(baseEvents[0].params.at("s"));
    int baseH = std::stoi(baseEvents[0].params.at("v"));
    int scaledW = std::stoi(scaledEvents[0].params.at("s"));
    int scaledH = std::stoi(scaledEvents[0].params.at("v"));

    assert(scaledW == baseW * 3);
    assert(scaledH == baseH * 3);

    unsetenv("MRIV_TEST_RENDER");
}

void testAxisChangesImage(const char* fixturePath)
{
    setenv("MRIV_TEST_RENDER", "1", 1);

    auto renderAxis = [&](const char* axis) -> std::string {
        std::istringstream in;
        std::ostringstream out, err;
        int rc = run(Args{"mriv", "--axis", axis, fixturePath}.argc(),
                     Args{"mriv", "--axis", axis, fixturePath}.data(), in, out, err);
        assert(rc == 0);
        return out.str();
    };

    assert(renderAxis("z") != renderAxis("y"));

    unsetenv("MRIV_TEST_RENDER");
}

void testMultipleFilesProduceLabelledStripInOrder(const char* fixtureA, const char* fixtureB)
{
    // M4: multiple positional files render as a strip -- one Kitty image
    // per file, each captioned with its path, in argument order.
    //
    // Two *different* fixtures matter here: passing the same file N times
    // makes every payload identical, so the test could only count images
    // and would sail through a reordering or a dropped file. Ordering is
    // asserted via the captions, which the escape parser surfaces as Text
    // events interleaved with the images.
    setenv("MRIV_TEST_RENDER", "1", 1);

    std::istringstream in;
    std::ostringstream out, err;
    int rc = run(Args{"mriv", fixtureA, fixtureB, fixtureA}.argc(),
                 Args{"mriv", fixtureA, fixtureB, fixtureA}.data(), in, out, err);
    assert(rc == 0);

    auto events = parseEscapeStream(out.str());

    // Expect strictly alternating caption/image pairs, three of each.
    std::vector<std::string> captions;
    int imageCount = 0;
    for (const auto& e : events)
    {
        if (e.kind == EventKind::Text)
        {
            // A caption must always precede its image.
            assert(captions.size() == static_cast<std::size_t>(imageCount));
            captions.push_back(e.payload);
        }
        else if (e.kind == EventKind::KittyGraphics)
        {
            ++imageCount;
            assert(captions.size() == static_cast<std::size_t>(imageCount));
        }
    }
    assert(imageCount == 3);
    assert(captions.size() == 3);

    assert(captions[0].find(fixtureA) != std::string::npos);
    assert(captions[1].find(fixtureB) != std::string::npos);
    assert(captions[2].find(fixtureA) != std::string::npos);

    unsetenv("MRIV_TEST_RENDER");
}

void testSingleFileIsNotLabelled(const char* fixturePath)
{
    // A one-file run stays pure image bytes: no caption, nothing but the
    // image. This is the "cat for medical images" case.
    setenv("MRIV_TEST_RENDER", "1", 1);

    std::istringstream in;
    std::ostringstream out, err;
    int rc = run(Args{"mriv", fixturePath}.argc(),
                 Args{"mriv", fixturePath}.data(), in, out, err);
    assert(rc == 0);

    auto events = parseEscapeStream(out.str());
    assert(events.size() == 1);
    assert(events[0].kind == EventKind::KittyGraphics);

    unsetenv("MRIV_TEST_RENDER");
}

void testMultipleFilesInfoPrintsEachInOrder(const char* fixtureA, const char* fixtureB)
{
    // --info with multiple files prints metadata for each, in argument
    // order, and still emits no image events. formatVolumeInfo() heads each
    // block with "=== <path> ===", which is what makes order checkable.
    std::istringstream in;
    std::ostringstream out, err;
    int rc = run(Args{"mriv", "--info", fixtureA, fixtureB}.argc(),
                 Args{"mriv", "--info", fixtureA, fixtureB}.data(), in, out, err);
    assert(rc == 0);

    const std::string& text = out.str();
    std::size_t posA = text.find(fixtureA);
    std::size_t posB = text.find(fixtureB);
    assert(posA != std::string::npos);
    assert(posB != std::string::npos);
    assert(posA < posB);

    std::size_t firstDims = text.find("dimensions:");
    assert(firstDims != std::string::npos);
    assert(text.find("dimensions:", firstDims + 1) != std::string::npos);

    auto events = parseEscapeStream(text);
    for (const auto& e : events)
        assert(e.kind != EventKind::KittyGraphics);
}

void testOneMissingFileIsSkippedNotFatal(const char* fixturePath)
{
    // A bad path in a glob must not cost the user the rest of the strip:
    // warn about the file that failed, render the ones that loaded, and
    // exit 0 because something was actually drawn.
    setenv("MRIV_TEST_RENDER", "1", 1);

    std::istringstream in;
    std::ostringstream out, err;
    int rc = run(Args{"mriv", fixturePath, "/does/not/exist.mnc"}.argc(),
                 Args{"mriv", fixturePath, "/does/not/exist.mnc"}.data(), in, out, err);
    assert(rc == 0);
    assert(err.str().find("failed to load") != std::string::npos);

    auto events = parseEscapeStream(out.str());
    int imageCount = 0;
    for (const auto& e : events)
        if (e.kind == EventKind::KittyGraphics)
            ++imageCount;
    assert(imageCount == 1);

    unsetenv("MRIV_TEST_RENDER");
}

void testAllFilesMissingFails()
{
    // ...but a run that renders nothing at all is still an error.
    setenv("MRIV_TEST_RENDER", "1", 1);

    std::istringstream in;
    std::ostringstream out, err;
    int rc = run(Args{"mriv", "/does/not/exist.mnc", "/also/missing.mnc"}.argc(),
                 Args{"mriv", "/does/not/exist.mnc", "/also/missing.mnc"}.data(), in, out, err);
    assert(rc != 0);

    auto events = parseEscapeStream(out.str());
    for (const auto& e : events)
        assert(e.kind != EventKind::KittyGraphics);

    unsetenv("MRIV_TEST_RENDER");
}

void testNoPixelSupportReportsOnceForStrip(const char* fixtureA, const char* fixtureB)
{
    // The no-pixel message is a fact about the terminal, not about each
    // file: an N-file strip must produce exactly one of them (it used to
    // produce N, each after a wasted volume load), and no images.
    setenv("MRIV_TEST_RENDER", "none", 1);

    std::istringstream in;
    std::ostringstream out, err;
    int rc = run(Args{"mriv", fixtureA, fixtureB, fixtureA}.argc(),
                 Args{"mriv", fixtureA, fixtureB, fixtureA}.data(), in, out, err);
    assert(rc == 0); // no --require-pixels: not an error, just nothing drawn

    const std::string& e = err.str();
    std::size_t first = e.find("no pixel graphics protocol");
    assert(first != std::string::npos);
    assert(e.find("no pixel graphics protocol", first + 1) == std::string::npos);

    auto events = parseEscapeStream(out.str());
    for (const auto& ev : events)
        assert(ev.kind != EventKind::KittyGraphics);

    unsetenv("MRIV_TEST_RENDER");
}

void testRequirePixelsExitsNonZeroWithoutPixelSupport(const char* fixturePath)
{
    setenv("MRIV_TEST_RENDER", "none", 1);

    std::istringstream in;
    std::ostringstream out, err;
    int rc = run(Args{"mriv", "--require-pixels", fixturePath}.argc(),
                 Args{"mriv", "--require-pixels", fixturePath}.data(), in, out, err);
    assert(rc != 0);
    assert(err.str().find("no pixel graphics protocol") != std::string::npos);

    auto events = parseEscapeStream(out.str());
    for (const auto& ev : events)
        assert(ev.kind != EventKind::KittyGraphics);

    unsetenv("MRIV_TEST_RENDER");
}

// --- interactive mode ----------------------------------------------------
//
// The tests here run with stdout redirected to a pipe by CTest, so
// isatty(STDOUT_FILENO) is false and interactive mode can never be entered.
// That is exactly the boundary worth pinning: the auto-detection must not
// fire, and an explicit --interactive must be refused with a reason rather
// than silently degrading to a one-shot render. The loop itself is covered
// by test_session, the navigation by test_view_state; only Screen needs a
// real terminal, and it is deliberately too thin to hold logic.

int countKittyImages(const std::string& bytes)
{
    int images = 0;
    for (const auto& ev : parseEscapeStream(bytes))
        if (ev.kind == EventKind::KittyGraphics)
            ++images;
    return images;
}

/// Without a TTY, a plain single-file invocation stays one-shot -- otherwise
/// piping mriv into a file would block forever waiting for keys.
void testNoTtyDoesNotEnterInteractiveMode(const char* fixture)
{
    setenv("MRIV_TEST_RENDER", "1", 1);
    Args args{"mriv", fixture};
    std::istringstream in;
    std::ostringstream out, err;

    int rc = run(args.argc(), args.data(), in, out, err);
    assert(rc == 0);

    // A one-shot render: exactly one image, and the process returned.
    assert(countKittyImages(out.str()) == 1);

    unsetenv("MRIV_TEST_RENDER");
}

void testExplicitInteractiveWithoutATtyIsRefused(const char* fixture)
{
    setenv("MRIV_TEST_RENDER", "1", 1);
    Args args{"mriv", "--interactive", fixture};
    std::istringstream in;
    std::ostringstream out, err;

    int rc = run(args.argc(), args.data(), in, out, err);
    assert(rc == 1);
    // The refusal specifically, not just any message mentioning a terminal:
    // the no-pixel-support diagnostic also contains the word "terminal", and
    // matching that instead would let a broken refusal pass by entering
    // interactive mode and failing there.
    assert(err.str().find("needs a terminal on stdout") != std::string::npos);
    // Refused before anything was drawn.
    assert(countKittyImages(out.str()) == 0);

    unsetenv("MRIV_TEST_RENDER");
}

void testExplicitInteractiveWithMultipleFilesIsRefused(const char* fixture, const char* fixture2)
{
    setenv("MRIV_TEST_RENDER", "1", 1);
    Args args{"mriv", "--interactive", fixture, fixture2};
    std::istringstream in;
    std::ostringstream out, err;

    int rc = run(args.argc(), args.data(), in, out, err);
    assert(rc == 1);
    assert(err.str().find("needs exactly one file") != std::string::npos);

    unsetenv("MRIV_TEST_RENDER");
}

void testInteractiveWithInfoIsRefused(const char* fixture)
{
    Args args{"mriv", "--interactive", "--info", fixture};
    std::istringstream in;
    std::ostringstream out, err;

    int rc = run(args.argc(), args.data(), in, out, err);
    assert(rc == 1);
    assert(err.str().find("--info") != std::string::npos);
    // The refusal comes before --info would have printed anything.
    assert(out.str().find("dimensions:") == std::string::npos);
}

void testInteractiveAndNoInteractiveTogetherIsRefused(const char* fixture)
{
    Args args{"mriv", "--interactive", "--no-interactive", fixture};
    std::istringstream in;
    std::ostringstream out, err;

    int rc = run(args.argc(), args.data(), in, out, err);
    assert(rc == 1);
    assert(err.str().find("--no-interactive") != std::string::npos);
}

/// --no-interactive is accepted and harmless off a TTY, so scripts can pass
/// it unconditionally without having to know where their output is going.
void testNoInteractiveStillRendersOneShot(const char* fixture)
{
    setenv("MRIV_TEST_RENDER", "1", 1);
    Args args{"mriv", "--no-interactive", fixture};
    std::istringstream in;
    std::ostringstream out, err;

    int rc = run(args.argc(), args.data(), in, out, err);
    assert(rc == 0);
    assert(countKittyImages(out.str()) == 1);

    unsetenv("MRIV_TEST_RENDER");
}

} // namespace

int main(int argc, char** argv)
{
    // Assert rather than skip. This used to be `if (fixturePath)`, which
    // silently turned nine of the tests below into no-ops when CMake failed
    // to pass the fixture -- the suite reported "Passed" while asserting only
    // argument-parsing trivia. Fail loudly instead, matching
    // test_slice_geometry.cpp, so a wiring regression can never hide again.
    assert(argc > 2 && "test_cli requires two fixture paths: <sq1.mnc> <sq2.mnc>");
    const char* fixturePath  = argv[1];
    const char* fixturePath2 = argv[2];

    testHelpReturnsZeroAndUsage();
    testVersionReturnsZero();
    testUnknownFlagFails();
    testNoFilesFails();
    testMissingFileFails();

    testInfoProducesNoImageEvents(fixturePath);
    testRenderPipelineProducesKittyImage(fixturePath);
    testMaxWidthCap(fixturePath);
    testRangeFlagChangesImage(fixturePath);
    testScaleFlagMagnifiesImage(fixturePath);
    testAxisChangesImage(fixturePath);
    testMultipleFilesProduceLabelledStripInOrder(fixturePath, fixturePath2);
    testSingleFileIsNotLabelled(fixturePath);
    testMultipleFilesInfoPrintsEachInOrder(fixturePath, fixturePath2);
    testOneMissingFileIsSkippedNotFatal(fixturePath);
    testAllFilesMissingFails();
    testNoPixelSupportReportsOnceForStrip(fixturePath, fixturePath2);
    testRequirePixelsExitsNonZeroWithoutPixelSupport(fixturePath);

    testNoTtyDoesNotEnterInteractiveMode(fixturePath);
    testExplicitInteractiveWithoutATtyIsRefused(fixturePath);
    testExplicitInteractiveWithMultipleFilesIsRefused(fixturePath, fixturePath2);
    testInteractiveWithInfoIsRefused(fixturePath);
    testInteractiveAndNoInteractiveTogetherIsRefused(fixturePath);
    testNoInteractiveStillRendersOneShot(fixturePath);

    return 0;
}
