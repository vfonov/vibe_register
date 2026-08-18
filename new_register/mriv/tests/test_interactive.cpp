/// test_interactive.cpp — the two pieces of interactive mode that are pure
/// decisions rather than terminal work: whether to enter it at all, and what
/// the status line says.
///
/// Both matter more than they look. The entry decision changes what plain
/// `mriv file.mnc` does on a TTY, and the status line is the only thing
/// telling a user which keys exist -- there is no help panel in this mode.

#include <cassert>
#include <string>
#include <string>

#include "cli/InteractiveDecision.hpp"
#include "interactive/StatusLine.hpp"

using namespace mriv::term;

namespace
{

/// The tests below predate multi-volume mode; this keeps them expressing
/// one volume with all three planes so they stay about the loop, not the
/// layout.
ViewState makeViewState(const glm::ivec3& dims, char axis, int slice,
                        double low, double high)
{
    glm::ivec3 cursor(dims.x / 2, dims.y / 2, dims.z / 2);
    switch (axis)
    {
        case 'x': cursor.x = slice; break;
        case 'y': cursor.y = slice; break;
        default:  cursor.z = slice; break;
    }

    VolumeDisplay display;
    display.rangeLow  = low;
    display.rangeHigh = high;
    return ViewState({dims}, {0, 1, 2}, axis, cursor, {display});
}

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

/// Two volumes of different depth, so the shared-cursor mapping shows up in
/// what the line reports.
ViewState makeTwoVolumeState()
{
    VolumeDisplay first;
    first.rangeLow  = 0.0;
    first.rangeHigh = 100.0;
    first.colourMap = ColourMapType::Spectral;

    VolumeDisplay second;
    second.rangeLow  = 0.0;
    second.rangeHigh = 400.0;

    return ViewState({kDims, glm::ivec3(64, 229, 48)}, {0, 1, 2}, 'z',
                     glm::ivec3(32, 114, 48), {first, second});
}

void testStatusLineNamesThePlaneSliceAndRange()
{
    ViewState state = makeViewState(kDims, 'z', 48, 0.0, 100.0);
    std::string line = formatStatusLine(state, {"brain.mnc"});

    assert(contains(line, "brain.mnc"));
    assert(contains(line, "axial"));
    assert(contains(line, "49/96")); // 1-based; see below
    assert(contains(line, "0"));
    assert(contains(line, "100"));
}

void testStatusLineNamesEachPlane()
{
    ViewState axial = makeViewState(kDims, 'z', 0, 0.0, 1.0);
    assert(contains(formatStatusLine(axial, {"v.mnc"}), "axial"));

    ViewState sagittal = makeViewState(kDims, 'x', 0, 0.0, 1.0);
    assert(contains(formatStatusLine(sagittal, {"v.mnc"}), "sagittal"));

    ViewState coronal = makeViewState(kDims, 'y', 0, 0.0, 1.0);
    assert(contains(formatStatusLine(coronal, {"v.mnc"}), "coronal"));
}

/// The slice number shown is 1-based, because "slice 1/96" reading "the
/// first of 96" is what a user expects from a position indicator, even
/// though the index passed to renderSlice() is 0-based.
void testStatusLineSliceNumberIsOneBased()
{
    ViewState first = makeViewState(kDims, 'z', 0, 0.0, 1.0);
    assert(contains(formatStatusLine(first, {"v.mnc"}), "1/96"));

    ViewState last = makeViewState(kDims, 'z', 95, 0.0, 1.0);
    assert(contains(formatStatusLine(last, {"v.mnc"}), "96/96"));
}

/// The key legend is part of the line: this mode has no help panel, so if
/// the legend goes missing the keys become undiscoverable.
void testStatusLineListsTheKeys()
{
    ViewState state = makeViewState(kDims, 'z', 0, 0.0, 1.0);
    std::string line = formatStatusLine(state, {"v.mnc"});

    assert(contains(line, "j/k"));
    assert(contains(line, "x/y/z"));
    assert(contains(line, "q"));
}

/// With more than one column the legend has to cover the keys that only
/// mean something there, and the single-volume case must not carry them --
/// a legend offering Tab when there is nothing to switch to is noise.
void testStatusLineListsColumnKeysOnlyWhenThereAreColumns()
{
    ViewState one = makeViewState(kDims, 'z', 0, 0.0, 1.0);
    assert(!contains(formatStatusLine(one, {"v.mnc"}), "Tab"));

    ViewState two = makeTwoVolumeState();
    std::string line = formatStatusLine(two, {"a.mnc", "b.mnc"});
    assert(contains(line, "Tab"));
}

/// Every column is named, in order, and the active one is marked -- 'c' and
/// the range prompt act on it, so which one it is has to be visible.
void testStatusLineNamesEveryVolumeAndMarksTheActiveOne()
{
    ViewState state = makeTwoVolumeState();

    std::string first = formatStatusLine(state, {"a.mnc", "b.mnc"});
    assert(contains(first, "a.mnc"));
    assert(contains(first, "b.mnc"));
    assert(first.find("a.mnc") < first.find("b.mnc"));
    assert(contains(first, "a.mnc*"));
    assert(!contains(first, "b.mnc*"));

    state.handleKey('\t');
    std::string second = formatStatusLine(state, {"a.mnc", "b.mnc"});
    assert(contains(second, "b.mnc*"));
    assert(!contains(second, "a.mnc*"));
}

/// The range and colour map shown are the active volume's: they are what
/// 'c' and 'r' would change.
void testStatusLineShowsTheActiveVolumesDisplay()
{
    ViewState state = makeTwoVolumeState();
    assert(contains(formatStatusLine(state, {"a.mnc", "b.mnc"}), "100"));
    assert(contains(formatStatusLine(state, {"a.mnc", "b.mnc"}), "Spectral"));

    state.handleKey('\t');
    assert(contains(formatStatusLine(state, {"a.mnc", "b.mnc"}), "400"));
}

/// Long paths would push the key legend off the row, so only the file name
/// is shown.
void testStatusLineShowsBaseNamesOnly()
{
    ViewState state = makeViewState(kDims, 'z', 0, 0.0, 1.0);
    std::string line = formatStatusLine(state, {"/data/study/sub-01/anat/t1.mnc"});
    assert(contains(line, "t1.mnc"));
    assert(!contains(line, "/data/study"));
}

/// One line, always -- it occupies a single reserved terminal row, and an
/// embedded newline would push the image down and corrupt the layout.
void testStatusLineIsASingleLine()
{
    ViewState state = makeViewState(kDims, 'y', 100, -1234.5678, 98765.4321);
    std::string line = formatStatusLine(state, {"some/long/path/to/a volume.nii.gz"});
    assert(line.find('\n') == std::string::npos);
    assert(line.find('\r') == std::string::npos);
}

/// Ranges span wildly different magnitudes across modalities, so the
/// formatting must stay compact rather than printing raw doubles.
void testStatusLineKeepsLargeRangesCompact()
{
    ViewState state = makeViewState(kDims, 'z', 0, 0.0, 32767.123456789);
    std::string line = formatStatusLine(state, {"v.mnc"});
    assert(!contains(line, "32767.123456789"));
    assert(contains(line, "3.277e+04") || contains(line, "32770") || contains(line, "3.277e+004"));
}

// --- range prompt --------------------------------------------------------

/// While the prompt is open the row shows it instead of the usual summary:
/// one reserved terminal row means the two cannot both be on screen, and
/// what is being typed is what matters at that moment.
void testStatusLineShowsThePromptWhileEditing()
{
    ViewState state = makeTwoVolumeState();
    state.handleKey('r');
    for (char key : std::string("20 180"))
        state.handleKey(key);

    std::string line = formatStatusLine(state, {"a.mnc", "b.mnc"});
    assert(contains(line, "range"));
    assert(contains(line, "a.mnc"));   // which column is being changed
    assert(contains(line, "20 180"));  // what has been typed so far
    assert(line.find('\n') == std::string::npos);
}

/// The prompt names the range it is replacing, so the user can see what the
/// numbers currently are before overwriting them.
void testPromptShowsTheRangeBeingReplaced()
{
    ViewState state = makeTwoVolumeState();
    state.handleKey('\t');
    state.handleKey('r');

    std::string line = formatStatusLine(state, {"a.mnc", "b.mnc"});
    assert(contains(line, "b.mnc"));
    assert(contains(line, "400"));
}

/// A rejected commit has to say so on the row, or the prompt just appears
/// to have stopped responding to Enter.
void testPromptReportsABadEntry()
{
    ViewState state = makeViewState(kDims, 'z', 0, 0.0, 1.0);
    state.handleKey('r');
    for (char key : std::string("nonsense"))
        state.handleKey(key);
    state.handleKey('\r');

    std::string line = formatStatusLine(state, {"v.mnc"});
    assert(contains(line, "nonsense"));
    assert(contains(line, "low high") || contains(line, "?"));
}

// --- one-shot caption ----------------------------------------------------

/// The one-shot grid is a single image, so the only way to tell the columns
/// apart is a caption naming them in order.
void testCaptionNumbersEveryColumnInOrder()
{
    std::string caption = formatCaption({"/tmp/a.mnc", "/tmp/b.mnc"});
    assert(contains(caption, "a.mnc"));
    assert(contains(caption, "b.mnc"));
    assert(caption.find("a.mnc") < caption.find("b.mnc"));
    assert(caption.find('\n') == std::string::npos);
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
    testStatusLineListsColumnKeysOnlyWhenThereAreColumns();
    testStatusLineNamesEveryVolumeAndMarksTheActiveOne();
    testStatusLineShowsTheActiveVolumesDisplay();
    testStatusLineShowsBaseNamesOnly();
    testStatusLineIsASingleLine();
    testStatusLineKeepsLargeRangesCompact();
    testStatusLineShowsThePromptWhileEditing();
    testPromptShowsTheRangeBeingReplaced();
    testPromptReportsABadEntry();
    testCaptionNumbersEveryColumnInOrder();

    return 0;
}
