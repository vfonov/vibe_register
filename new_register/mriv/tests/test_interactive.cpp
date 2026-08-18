/// test_interactive.cpp — the two pieces of interactive mode that are pure
/// decisions rather than terminal work: whether to enter it at all, and what
/// the status line says.
///
/// Both matter more than they look. The entry decision changes what plain
/// `mriv file.mnc` does on a TTY, and the status line is the only thing
/// telling a user which keys exist -- there is no help panel in this mode.

#include <cassert>
#include <string>

#include "cli/InteractiveDecision.hpp"
#include "interactive/StatusLine.hpp"

using namespace mriv::term;

namespace
{

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

Options makeOptions(int fileCount)
{
    Options options;
    for (int i = 0; i < fileCount; ++i)
        options.files.push_back("volume" + std::to_string(i) + ".mnc");
    return options;
}

// --- entry decision ------------------------------------------------------

/// The auto-detected case from PLAN.md: a TTY and exactly one file.
void testAutoDetectNeedsATtyAndOneFile()
{
    auto onTtyOneFile = decideInteractive(makeOptions(1), true);
    assert(onTtyOneFile.interactive);
    assert(onTtyOneFile.refusal.empty());

    auto piped = decideInteractive(makeOptions(1), false);
    assert(!piped.interactive);
    assert(piped.refusal.empty());

    auto strip = decideInteractive(makeOptions(3), true);
    assert(!strip.interactive);
    assert(strip.refusal.empty());
}

/// --info prints metadata and exits; there is nothing to navigate.
void testInfoIsNeverInteractive()
{
    Options options = makeOptions(1);
    options.info = true;

    auto decision = decideInteractive(options, true);
    assert(!decision.interactive);
    assert(decision.refusal.empty());
}

/// --no-interactive is the escape hatch for a user sitting at a TTY who
/// wants the one-shot "cat for medical images" behaviour anyway.
void testNoInteractiveForcesOneShot()
{
    Options options = makeOptions(1);
    options.noInteractive = true;

    auto decision = decideInteractive(options, true);
    assert(!decision.interactive);
    assert(decision.refusal.empty());
}

void testExplicitInteractiveOnATtyWithOneFile()
{
    Options options = makeOptions(1);
    options.interactive = true;

    auto decision = decideInteractive(options, true);
    assert(decision.interactive);
    assert(decision.refusal.empty());
}

/// An explicit --interactive that cannot be honoured is refused with a
/// reason rather than silently downgraded to a one-shot render: the user
/// asked for a navigable view, and quietly printing one slice instead looks
/// like the keys are broken.
void testExplicitInteractiveIsRefusedWithAReason()
{
    Options piped = makeOptions(1);
    piped.interactive = true;
    auto pipedDecision = decideInteractive(piped, false);
    assert(!pipedDecision.interactive);
    assert(contains(pipedDecision.refusal, "terminal"));

    Options strip = makeOptions(2);
    strip.interactive = true;
    auto stripDecision = decideInteractive(strip, true);
    assert(!stripDecision.interactive);
    assert(contains(stripDecision.refusal, "one file"));

    Options info = makeOptions(1);
    info.interactive = true;
    info.info = true;
    auto infoDecision = decideInteractive(info, true);
    assert(!infoDecision.interactive);
    assert(contains(infoDecision.refusal, "--info"));
}

/// Asking for both at once is a contradiction, not a precedence puzzle.
void testInteractiveAndNoInteractiveTogetherIsRefused()
{
    Options options = makeOptions(1);
    options.interactive = true;
    options.noInteractive = true;

    auto decision = decideInteractive(options, true);
    assert(!decision.interactive);
    assert(!decision.refusal.empty());
}

// --- status line ---------------------------------------------------------

const glm::ivec3 kDims{64, 229, 96};

void testStatusLineNamesThePlaneSliceAndRange()
{
    ViewState state(kDims, 'z', 48, 0.0, 100.0);
    std::string line = formatStatusLine(state, "brain.mnc");

    assert(contains(line, "brain.mnc"));
    assert(contains(line, "axial"));
    assert(contains(line, "49/96")); // 1-based; see below
    assert(contains(line, "0"));
    assert(contains(line, "100"));
}

void testStatusLineNamesEachPlane()
{
    ViewState axial(kDims, 'z', 0, 0.0, 1.0);
    assert(contains(formatStatusLine(axial, "v.mnc"), "axial"));

    ViewState sagittal(kDims, 'x', 0, 0.0, 1.0);
    assert(contains(formatStatusLine(sagittal, "v.mnc"), "sagittal"));

    ViewState coronal(kDims, 'y', 0, 0.0, 1.0);
    assert(contains(formatStatusLine(coronal, "v.mnc"), "coronal"));
}

/// The slice number shown is 1-based, because "slice 1/96" reading "the
/// first of 96" is what a user expects from a position indicator, even
/// though the index passed to renderSlice() is 0-based.
void testStatusLineSliceNumberIsOneBased()
{
    ViewState first(kDims, 'z', 0, 0.0, 1.0);
    assert(contains(formatStatusLine(first, "v.mnc"), "1/96"));

    ViewState last(kDims, 'z', 95, 0.0, 1.0);
    assert(contains(formatStatusLine(last, "v.mnc"), "96/96"));
}

/// The key legend is part of the line: this mode has no help panel, so if
/// the legend goes missing the keys become undiscoverable.
void testStatusLineListsTheKeys()
{
    ViewState state(kDims, 'z', 0, 0.0, 1.0);
    std::string line = formatStatusLine(state, "v.mnc");

    assert(contains(line, "j/k"));
    assert(contains(line, "x/y/z"));
    assert(contains(line, "+/-"));
    assert(contains(line, "q"));
}

/// One line, always -- it occupies a single reserved terminal row, and an
/// embedded newline would push the image down and corrupt the layout.
void testStatusLineIsASingleLine()
{
    ViewState state(kDims, 'y', 100, -1234.5678, 98765.4321);
    std::string line = formatStatusLine(state, "some/long/path/to/a volume.nii.gz");
    assert(line.find('\n') == std::string::npos);
    assert(line.find('\r') == std::string::npos);
}

/// Ranges span wildly different magnitudes across modalities, so the
/// formatting must stay compact rather than printing raw doubles.
void testStatusLineKeepsLargeRangesCompact()
{
    ViewState state(kDims, 'z', 0, 0.0, 32767.123456789);
    std::string line = formatStatusLine(state, "v.mnc");
    assert(!contains(line, "32767.123456789"));
    assert(contains(line, "3.277e+04") || contains(line, "32770") || contains(line, "3.277e+004"));
}

} // namespace

int main()
{
    testAutoDetectNeedsATtyAndOneFile();
    testInfoIsNeverInteractive();
    testNoInteractiveForcesOneShot();
    testExplicitInteractiveOnATtyWithOneFile();
    testExplicitInteractiveIsRefusedWithAReason();
    testInteractiveAndNoInteractiveTogetherIsRefused();

    testStatusLineNamesThePlaneSliceAndRange();
    testStatusLineNamesEachPlane();
    testStatusLineSliceNumberIsOneBased();
    testStatusLineListsTheKeys();
    testStatusLineIsASingleLine();
    testStatusLineKeepsLargeRangesCompact();

    return 0;
}
