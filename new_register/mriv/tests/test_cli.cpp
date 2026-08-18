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
    std::istringstream in;
    std::ostringstream out, err;
    int rc = run(Args{"mriv", "/does/not/exist.mnc"}.argc(),
                 Args{"mriv", "/does/not/exist.mnc"}.data(), in, out, err);
    assert(rc != 0);
    assert(err.str().find("failed to load") != std::string::npos);
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

} // namespace

int main(int argc, char** argv)
{
    const char* fixturePath = nullptr;
    if (argc > 1)
        fixturePath = argv[1];

    testHelpReturnsZeroAndUsage();
    testVersionReturnsZero();
    testUnknownFlagFails();
    testNoFilesFails();
    testMissingFileFails();

    // The following tests need a real fixture on disk.
    if (fixturePath)
    {
        testInfoProducesNoImageEvents(fixturePath);
        testRenderPipelineProducesKittyImage(fixturePath);
        testMaxWidthCap(fixturePath);
        testRangeFlagChangesImage(fixturePath);
        testScaleFlagMagnifiesImage(fixturePath);
        testAxisChangesImage(fixturePath);
    }

    return 0;
}
