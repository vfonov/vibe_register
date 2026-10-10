/// test_interactive.cpp — the two pieces of interactive mode that are pure
/// decisions rather than terminal work: whether to enter it at all, and what
/// the status line says.
///
/// Both matter more than they look. The entry decision changes what plain
/// `mriv file.mnc` does on a TTY, and the status and legend rows
/// are what tell a user where they are and which keys exist, including
/// '?' for the full list.

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
    return ViewState({VolumeGeometry{dims}}, {0, 1, 2}, axis, cursor, {display});
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

/// Interactive mode needs a terminal to read keys from, and nothing else.
/// The file count no longer decides it: several volumes become columns of
/// one navigable grid, which is more useful interactively than piped.
void testAutoDetectNeedsATty()
{
    auto onTtyOneFile = decideInteractive(makeOptions(1), true);
    assert(onTtyOneFile.interactive);

    auto onTtyManyFiles = decideInteractive(makeOptions(3), true);
    assert(onTtyManyFiles.interactive);
    assert(onTtyOneFile.refusal.empty());

    auto piped = decideInteractive(makeOptions(1), false);
    assert(!piped.interactive);
    assert(piped.refusal.empty());

    auto pipedMany = decideInteractive(makeOptions(3), false);
    assert(!pipedMany.interactive);
    assert(pipedMany.refusal.empty());
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

void testExplicitInteractiveOnATty()
{
    Options options = makeOptions(1);
    options.interactive = true;

    auto decision = decideInteractive(options, true);
    assert(decision.interactive);
    assert(decision.refusal.empty());

    // Several volumes are columns of one grid, not a reason to refuse.
    Options many = makeOptions(4);
    many.interactive = true;

    auto manyDecision = decideInteractive(many, true);
    assert(manyDecision.interactive);
    assert(manyDecision.refusal.empty());
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

    return ViewState({VolumeGeometry{kDims}, VolumeGeometry{glm::ivec3(64, 229, 48)}}, {0, 1, 2}, 'z',
                     glm::ivec3(32, 114, 48), {first, second});
}

void testStatusLineNamesThePlaneSliceAndRange()
{
    ViewState state = makeViewState(kDims, 'z', 48, 0.0, 100.0);
    std::string line = formatStatusLine(state, {"brain.mnc"});

    assert(contains(line, "axial"));
    assert(contains(line, "49/96")); // 1-based; see below
    assert(contains(line, "0"));
    assert(contains(line, "100"));
}

/// The volume names live above the status row, one per line, plain -- which
/// one is active is a physical marker Overlay draws under its column, not
/// text here.
void testVolumeNameLinesNameEachColumnInOrderWithNoMarker()
{
    auto lines = formatVolumeNameLines({"/data/a.mnc", "/data/b.mnc"});
    assert(lines.size() == 2);
    assert(lines[0] == "a.mnc");
    assert(lines[1] == "b.mnc");
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

/// The key legend has its own row: it is the only reminder of the keys
/// on screen with the picture, '?' included.
void testHotkeyLineListsTheKeys()
{
    ViewState state = makeViewState(kDims, 'z', 0, 0.0, 1.0);
    std::string line = formatHotkeyLine(state);

    assert(contains(line, "j/k"));
    assert(contains(line, "x/y/z"));
    assert(contains(line, "s screenshot"));
    assert(contains(line, "q"));
}

/// With more than one column the legend has to cover the keys that only
/// mean something there, and the single-volume case must not carry them --
/// a legend offering Tab when there is nothing to switch to is noise.
void testHotkeyLineListsColumnKeysOnlyWhenThereAreColumns()
{
    ViewState one = makeViewState(kDims, 'z', 0, 0.0, 1.0);
    assert(!contains(formatHotkeyLine(one), "Tab"));

    ViewState two = makeTwoVolumeState();
    std::string line = formatHotkeyLine(two);
    assert(contains(line, "Tab"));
}

/// One line, always -- like formatStatusLine(), it occupies a single
/// reserved terminal row.
void testHotkeyLineIsASingleLine()
{
    ViewState state = makeTwoVolumeState();
    std::string line = formatHotkeyLine(state);
    assert(line.find('\n') == std::string::npos);
    assert(line.find('\r') == std::string::npos);
}

/// The legend row only has room for the everyday keys, so it has to say how
/// to reach the full list, and how to hide the info block above it.
void testHotkeyLineAdvertisesHelpAndInfo()
{
    ViewState state = makeViewState(kDims, 'z', 0, 0.0, 1.0);
    std::string line = formatHotkeyLine(state);
    assert(contains(line, "? help"));
    assert(contains(line, "i info"));
}

/// The info block: one row per loaded file with the cursor in that file's
/// own voxels, then the world position they all share.
void testInfoLinesListFilesAndCursorPositions()
{
    ViewState state = makeTwoVolumeState();
    auto lines = formatInfoLines(state, {"/data/a.mnc", "/data/longer_b.mnc"});

    assert(lines.size() == 3);
    assert(contains(lines[0], "a.mnc"));
    assert(!contains(lines[0], "/data"));
    assert(contains(lines[0], "voxel 32 114 48"));
    assert(contains(lines[1], "longer_b.mnc"));
    assert(contains(lines[1], "voxel 32 114 48"));
    assert(contains(lines[2], "world 32.00 114.00 48.00"));

    // Names are padded to one width so the voxel columns line up.
    assert(lines[0].find("voxel") == lines[1].find("voxel"));

    for (const auto& line : lines)
        assert(line.find('\n') == std::string::npos);
}

/// z = 48 is past the end of the 48-slice second volume: its column shows
/// background there, and the info row says why.
void testInfoLinesMarkAVolumeTheCursorIsOutside()
{
    ViewState state = makeTwoVolumeState();
    auto lines = formatInfoLines(state, {"a.mnc", "b.mnc"});
    assert(!contains(lines[0], "outside"));
    assert(contains(lines[1], "outside"));
}

/// The info block follows the cursor.
void testInfoLinesFollowTheCursor()
{
    ViewState state = makeViewState(kDims, 'z', 10, 0.0, 1.0);
    state.handleKey('j');
    auto lines = formatInfoLines(state, {"v.mnc"});
    assert(lines.size() == 2);
    assert(contains(lines[0], "voxel 32 114 11"));
}

/// The '?' screen has to cover every binding handleKey() acts on -- it is
/// the only complete list the user gets.
void testHelpLinesListEveryKey()
{
    ViewState state = makeTwoVolumeState();
    auto lines = formatHelpLines(state);

    std::string all;
    for (const auto& line : lines)
    {
        assert(line.find('\n') == std::string::npos);
        all += line + "\n";
    }

    for (const char* key : {"j / k", "x / y / z", "Up / Down", "Tab", "1-9", "Left / Right",
                            "c / C", "r", "s", "i", "? / h", "q, Esc", "Ctrl-L"})
        assert(contains(all, key));
    assert(contains(all, "any key"));
}

/// With one volume there are no columns to switch between, so the column
/// keys are left out, the same rule the legend row follows.
void testHelpLinesOmitColumnKeysForOneVolume()
{
    ViewState one = makeViewState(kDims, 'z', 0, 0.0, 1.0);
    std::string all;
    for (const auto& line : formatHelpLines(one))
        all += line + "\n";
    assert(!contains(all, "Tab"));
    assert(contains(all, "j / k"));
}

/// The overlay key is a column key: offered when there are volumes to
/// blend, left out when there is only one.
void testOverlayKeyIsListedOnlyWhenThereAreColumns()
{
    auto helpText = [](const ViewState& state) {
        std::string all;
        for (const auto& line : formatHelpLines(state))
            all += line + "\n";
        return all;
    };

    ViewState two = makeTwoVolumeState();
    assert(contains(formatHotkeyLine(two), "o overlay"));
    assert(contains(helpText(two), "overlay column"));

    ViewState one = makeViewState(kDims, 'z', 0, 0.0, 1.0);
    assert(!contains(formatHotkeyLine(one), "overlay"));
    assert(!contains(helpText(one), "overlay"));
}

/// The overlay column has no file to name it, so while it is shown the
/// info block says what the last column is.
void testInfoLinesExplainTheOverlayColumn()
{
    ViewState state = makeTwoVolumeState();
    auto before = formatInfoLines(state, {"a.mnc", "b.mnc"});
    assert(before.size() == 3);
    for (const auto& line : before)
        assert(!contains(line, "overlay"));

    state.handleKey('o');
    auto lines = formatInfoLines(state, {"a.mnc", "b.mnc"});
    assert(lines.size() == 4);
    assert(contains(lines[2], "overlay"));
    assert(contains(lines[2], "blend of 2 volumes"));
    assert(contains(lines[3], "world"));
}

/// While the go-to prompt is open the status row shows it, with the plane
/// and the valid range, so the user knows what the number means.
void testStatusLineShowsTheGoToPrompt()
{
    ViewState state = makeViewState(kDims, 'z', 10, 0.0, 1.0);
    state.handleKey('g');
    state.handleKey('4');
    state.handleKey('2');

    std::string line = formatStatusLine(state, {"v.mnc"});
    assert(contains(line, "go to axial slice"));
    assert(contains(line, "1-96"));
    assert(contains(line, "42_"));
    assert(!contains(line, "range"));
    assert(!contains(line, "expected"));

    state.handleKey('0');
    state.handleKey('\r');
    assert(contains(formatStatusLine(state, {"v.mnc"}), "expected 1 to 96"));
}

void testHelpAndLegendCoverTheNewNavigationKeys()
{
    ViewState state = makeViewState(kDims, 'z', 0, 0.0, 1.0);
    std::string all;
    for (const auto& line : formatHelpLines(state))
        all += line + "\n";
    assert(contains(all, "J / K"));
    assert(contains(all, "  g "));
    assert(contains(all, "  m "));
    assert(contains(formatHotkeyLine(state), "g goto"));
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
void testVolumeNameLinesShowBaseNamesOnly()
{
    auto lines = formatVolumeNameLines({"/data/study/sub-01/anat/t1.mnc"});
    assert(lines.size() == 1);
    assert(contains(lines[0], "t1.mnc"));
    assert(!contains(lines[0], "/data/study"));
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

// --- exit summary --------------------------------------------------------

/// Quitting leaves the last frame on the terminal, captioned with where the
/// user ended up. The key legend has no business there -- the keys are gone
/// -- but everything identifying the picture must survive.
void testSummaryLineKeepsThePositionAndDropsTheLegend()
{
    ViewState state = makeTwoVolumeState();
    std::string summary = formatSummaryLine(state, {"a.mnc", "b.mnc"});

    assert(contains(summary, "axial"));
    assert(contains(summary, "49/96"));
    assert(!contains(summary, "j/k"));
    assert(!contains(summary, "q quit"));
    assert(summary.find('\n') == std::string::npos);
}

/// While idle, the status row and the exit-time summary say exactly the
/// same thing -- the legend used to be appended to one and not the other,
/// but now that it is its own row there is nothing left to distinguish
/// them.
void testStatusLineEqualsSummaryLineWhenNotEditing()
{
    ViewState state = makeTwoVolumeState();
    std::string summary = formatSummaryLine(state, {"a.mnc", "b.mnc"});
    std::string status  = formatStatusLine(state, {"a.mnc", "b.mnc"});

    assert(status == summary);
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

/// The zoom is on the status row only while it is not 1, so the row is
/// unchanged for anyone who never zooms.
void testStatusLineShowsTheZoomWhenZoomed()
{
    ViewState state = makeViewState(kDims, 'z', 48, 0.0, 100.0);
    assert(!contains(formatStatusLine(state, {"brain.mnc"}), "zoom"));

    state.setZoom(2.0);
    assert(contains(formatStatusLine(state, {"brain.mnc"}), "zoom 2x"));

    state.setZoom(1.0);
    state.handleKey('-');
    assert(contains(formatStatusLine(state, {"brain.mnc"}), "zoom 0.8x"));
}

void testLegendAndHelpListTheZoomKeys()
{
    ViewState state = makeViewState(kDims, 'z', 48, 0.0, 100.0);

    assert(contains(formatHotkeyLine(state), "+/- zoom"));

    bool zoomRow = false;
    bool resetRow = false;
    for (const auto& line : formatHelpLines(state))
    {
        zoomRow  = zoomRow || (contains(line, "+ / -") && contains(line, "zoom"));
        resetRow = resetRow || (contains(line, "  0 ") && contains(line, "zoom"));
    }
    assert(zoomRow);
    assert(resetRow);
}

} // namespace

int main()
{
    testAutoDetectNeedsATty();
    testInfoIsNeverInteractive();
    testNoInteractiveForcesOneShot();
    testExplicitInteractiveOnATty();
    testExplicitInteractiveIsRefusedWithAReason();
    testInteractiveAndNoInteractiveTogetherIsRefused();

    testStatusLineNamesThePlaneSliceAndRange();
    testStatusLineNamesEachPlane();
    testStatusLineSliceNumberIsOneBased();
    testHotkeyLineListsTheKeys();
    testHotkeyLineListsColumnKeysOnlyWhenThereAreColumns();
    testHotkeyLineIsASingleLine();
    testHotkeyLineAdvertisesHelpAndInfo();
    testInfoLinesListFilesAndCursorPositions();
    testInfoLinesMarkAVolumeTheCursorIsOutside();
    testInfoLinesFollowTheCursor();
    testHelpLinesListEveryKey();
    testHelpLinesOmitColumnKeysForOneVolume();
    testOverlayKeyIsListedOnlyWhenThereAreColumns();
    testInfoLinesExplainTheOverlayColumn();
    testStatusLineShowsTheGoToPrompt();
    testHelpAndLegendCoverTheNewNavigationKeys();
    testStatusLineShowsTheActiveVolumesDisplay();
    testVolumeNameLinesShowBaseNamesOnly();
    testVolumeNameLinesNameEachColumnInOrderWithNoMarker();
    testStatusLineIsASingleLine();
    testStatusLineKeepsLargeRangesCompact();
    testStatusLineShowsThePromptWhileEditing();
    testPromptShowsTheRangeBeingReplaced();
    testPromptReportsABadEntry();
    testSummaryLineKeepsThePositionAndDropsTheLegend();
    testStatusLineEqualsSummaryLineWhenNotEditing();
    testCaptionNumbersEveryColumnInOrder();
    testStatusLineShowsTheZoomWhenZoomed();
    testLegendAndHelpListTheZoomKeys();

    return 0;
}
