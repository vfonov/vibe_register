/// test_view_state.cpp — the interactive session's model.
///
/// ViewState is where every interactive decision lives, precisely because
/// the display glue around it cannot be tested on a host with no TTY and no
/// pixel protocol (mriv/HANDOFF.md sec 3.9). Anything asserted here is
/// behaviour that would otherwise only be checkable by eye.

#include <cassert>
#include <string>
#include <vector>

#include "interactive/ViewState.hpp"

using namespace mriv::term;

namespace
{

std::vector<VolumeDisplay> displaysFor(size_t count)
{
    std::vector<VolumeDisplay> displays(count);
    for (auto& display : displays)
    {
        display.rangeLow  = 0.0;
        display.rangeHigh = 100.0;
    }
    return displays;
}

/// One volume, all three planes, cursor at the centre.
ViewState makeState(const glm::ivec3& dims = glm::ivec3(64, 128, 96),
                    char axis = 'z')
{
    glm::ivec3 cursor(dims.x / 2, dims.y / 2, dims.z / 2);
    return ViewState({dims}, {0, 1, 2}, axis, cursor, displaysFor(1));
}

/// Two volumes of different depth, to pin the synchronised cursor.
ViewState makeTwoVolumeState()
{
    glm::ivec3 first(64, 128, 96);
    glm::ivec3 second(64, 128, 48);
    glm::ivec3 cursor(32, 64, 48);
    return ViewState({first, second}, {0, 1, 2}, 'z', cursor, displaysFor(2));
}

void testViewsAndVolumeCount()
{
    auto state = makeState();
    assert(state.views().size() == 3);
    assert(state.volumeCount() == 1);
    assert(state.activeVolume() == 0);
    assert(state.axis() == 'z');
    assert(state.viewIndex() == 0);
}

/// The active axis decides what j/k moves; every displayed view still has
/// its own position, taken from the shared 3D cursor.
void testEachViewKeepsItsOwnSlice()
{
    auto state = makeState();
    assert(state.sliceIndexFor(0, 0) == 48); // axial   -> Z
    assert(state.sliceIndexFor(0, 1) == 32); // sagittal-> X
    assert(state.sliceIndexFor(0, 2) == 64); // coronal -> Y
}

void testJAndKMoveTheActiveAxisOnly()
{
    auto state = makeState();
    assert(state.handleKey('j') == KeyResult::Changed);
    assert(state.sliceIndexFor(0, 0) == 49);
    assert(state.sliceIndexFor(0, 1) == 32);
    assert(state.sliceIndexFor(0, 2) == 64);

    assert(state.handleKey('k') == KeyResult::Changed);
    assert(state.sliceIndexFor(0, 0) == 48);
}

void testSliceNavigationClampsAtBothEnds()
{
    auto state = makeState(glm::ivec3(4, 4, 4));
    for (int i = 0; i < 10; ++i)
        state.handleKey('j');
    assert(state.sliceIndex() == 3);
    assert(state.handleKey('j') == KeyResult::Ignored);

    for (int i = 0; i < 10; ++i)
        state.handleKey('k');
    assert(state.sliceIndex() == 0);
    assert(state.handleKey('k') == KeyResult::Ignored);
}

/// Leaving an axis and coming back lands exactly where it was left: the
/// three axes are geometrically independent, so there is no meaningful way
/// to carry a position from one to another.
void testAxisSwitchIsLosslessRoundTrip()
{
    auto state = makeState();
    state.handleKey('j');
    state.handleKey('j');
    int axialSlice = state.sliceIndex();

    assert(state.handleKey('x') == KeyResult::Changed);
    assert(state.axis() == 'x');
    assert(state.viewIndex() == 1);
    state.handleKey('j');

    assert(state.handleKey('z') == KeyResult::Changed);
    assert(state.sliceIndex() == axialSlice);
}

void testReselectingTheCurrentAxisIsIgnored()
{
    auto state = makeState();
    assert(state.handleKey('z') == KeyResult::Ignored);
}

/// An axis whose view is not on screen cannot be made active: pressing it
/// would silently move a slice the user cannot see.
void testAxisNotDisplayedIsIgnored()
{
    ViewState state({glm::ivec3(64, 128, 96)}, {0}, 'z', glm::ivec3(32, 64, 48),
                    displaysFor(1));
    assert(state.handleKey('x') == KeyResult::Ignored);
    assert(state.axis() == 'z');
}

/// If --axis names a plane --views does not show, the active axis falls
/// back to the first displayed view rather than being stranded off screen.
void testActiveAxisFallsBackToADisplayedView()
{
    ViewState state({glm::ivec3(64, 128, 96)}, {2}, 'z', glm::ivec3(32, 64, 48),
                    displaysFor(1));
    assert(state.viewIndex() == 2);
    assert(state.axis() == 'y');
}

/// Every column moves together. Volume 1 has half the slices of volume 0,
/// so it tracks proportionally rather than stalling or running off the end.
void testSliceNavigationIsSynchronisedAcrossVolumes()
{
    auto state = makeTwoVolumeState();
    assert(state.sliceIndexFor(0, 0) == 48);
    assert(state.sliceIndexFor(1, 0) == mapSliceIndex(48, 96, 48));

    state.handleKey('j');
    assert(state.sliceIndexFor(0, 0) == 49);
    assert(state.sliceIndexFor(1, 0) == mapSliceIndex(49, 96, 48));

    // The axes the volumes share exactly stay index-for-index equal.
    assert(state.sliceIndexFor(0, 1) == state.sliceIndexFor(1, 1));
    assert(state.sliceIndexFor(0, 2) == state.sliceIndexFor(1, 2));
}

/// The slice count reported for navigation is the first volume's -- that is
/// the space the shared cursor lives in.
void testSliceCountComesFromTheFirstVolume()
{
    auto state = makeTwoVolumeState();
    assert(state.sliceCount() == 96);
    assert(state.sliceCountFor(1, 0) == 48);
}

void testTabCyclesTheActiveVolume()
{
    auto state = makeTwoVolumeState();
    assert(state.activeVolume() == 0);
    assert(state.handleKey('\t') == KeyResult::Changed);
    assert(state.activeVolume() == 1);
    assert(state.handleKey('\t') == KeyResult::Changed);
    assert(state.activeVolume() == 0);
}

/// With one volume there is nothing to switch to, so Tab changes nothing
/// and must not ask for a repaint.
void testTabWithOneVolumeIsIgnored()
{
    auto state = makeState();
    assert(state.handleKey('\t') == KeyResult::Ignored);
}

void testDigitsSelectAVolumeDirectly()
{
    auto state = makeTwoVolumeState();
    assert(state.handleKey('2') == KeyResult::Changed);
    assert(state.activeVolume() == 1);
    assert(state.handleKey('1') == KeyResult::Changed);
    assert(state.activeVolume() == 0);
    // Already selected, and out of range.
    assert(state.handleKey('1') == KeyResult::Ignored);
    assert(state.handleKey('3') == KeyResult::Ignored);
    assert(state.activeVolume() == 0);
}

/// 'c' recolours only the active column: the whole point of separate
/// colour maps is telling two volumes apart.
void testColourMapCyclesOnTheActiveVolumeOnly()
{
    auto state = makeTwoVolumeState();
    ColourMapType firstBefore  = state.display(0).colourMap;
    ColourMapType secondBefore = state.display(1).colourMap;

    assert(state.handleKey('c') == KeyResult::Changed);
    assert(state.display(0).colourMap != firstBefore);
    assert(state.display(1).colourMap == secondBefore);

    // Back where it started.
    assert(state.handleKey('C') == KeyResult::Changed);
    assert(state.display(0).colourMap == firstBefore);
}

/// Cycling wraps in both directions rather than stopping at the ends.
void testColourMapCycleWraps()
{
    auto state = makeState();
    ColourMapType start = state.display(0).colourMap;
    for (int i = 0; i < colourMapCount(); ++i)
        state.handleKey('c');
    assert(state.display(0).colourMap == start);

    state.handleKey('C');
    assert(state.display(0).colourMap
           == static_cast<ColourMapType>(colourMapCount() - 1));
}

// --- range prompt --------------------------------------------------------

/// 'r' opens a prompt on the active column. While it is open the keys
/// belong to the editor, not to navigation: typing "j" into a number must
/// not also move a slice.
void testRangePromptSwallowsNavigationKeys()
{
    auto state = makeState();
    int before = state.sliceIndex();

    assert(state.handleKey('r') == KeyResult::Changed);
    assert(state.isEditing());

    state.handleKey('j');
    assert(state.sliceIndex() == before);
    assert(state.editor().text() == "j");
}

void testRangePromptCommitsToTheActiveVolume()
{
    auto state = makeTwoVolumeState();
    state.handleKey('\t');
    assert(state.activeVolume() == 1);

    state.handleKey('r');
    for (char key : std::string("20 180"))
        state.handleKey(key);
    assert(state.handleKey('\r') == KeyResult::Changed);

    assert(!state.isEditing());
    assert(state.display(1).rangeLow == 20.0);
    assert(state.display(1).rangeHigh == 180.0);
    // The other column is untouched.
    assert(state.display(0).rangeHigh == 100.0);
}

void testRangePromptCancelLeavesTheRangeAlone()
{
    auto state = makeState();
    double low  = state.display(0).rangeLow;
    double high = state.display(0).rangeHigh;

    state.handleKey('r');
    for (char key : std::string("20 180"))
        state.handleKey(key);
    assert(state.handleKey('\x1b') == KeyResult::Changed);

    assert(!state.isEditing());
    assert(state.display(0).rangeLow == low);
    assert(state.display(0).rangeHigh == high);
}

/// Esc closes the prompt rather than quitting: leaving the application
/// because a range was typed wrong would be a nasty surprise.
void testEscapeInThePromptDoesNotQuit()
{
    auto state = makeState();
    state.handleKey('r');
    assert(state.handleKey('\x1b') != KeyResult::Quit);
    // Now that the prompt is closed, Esc quits again.
    assert(state.handleKey('\x1b') == KeyResult::Quit);
}

/// A commit that will not parse keeps the prompt open with a complaint.
void testBadRangeKeepsThePromptOpen()
{
    auto state = makeState();
    state.handleKey('r');
    for (char key : std::string("nonsense"))
        state.handleKey(key);

    assert(state.handleKey('\r') == KeyResult::Changed);
    assert(state.isEditing());
    assert(state.editor().hasError());
    assert(state.display(0).rangeHigh == 100.0);
}

/// The prompt opens showing the range it would replace.
void testPromptStartsFromTheVolumesCurrentRange()
{
    auto state = makeState();
    state.handleKey('r');
    assert(state.editor().currentLow() == 0.0);
    assert(state.editor().currentHigh() == 100.0);
}

void testQuitAndUnknownKeys()
{
    auto state = makeState();
    assert(state.handleKey('q') == KeyResult::Quit);
    assert(state.handleKey('\x1b') == KeyResult::Quit);
    assert(state.handleKey('Z') == KeyResult::Ignored);
    assert(state.handleKey('\0') == KeyResult::Ignored);
    // The window keys are gone: ranges are typed in, not scaled.
    assert(state.handleKey('+') == KeyResult::Ignored);
    assert(state.handleKey('-') == KeyResult::Ignored);
}

/// 's' asks the caller to save a screenshot of the frame already on
/// screen; it must not touch cursor, axis, volume or display state, only
/// report the request via a distinct KeyResult so the caller does not
/// mistake it for a Changed that needs a redraw.
void testScreenshotKeyDoesNotChangeState()
{
    auto state = makeState();
    char axisBefore = state.axis();
    int sliceBefore = state.sliceIndex();
    int volumeBefore = state.activeVolume();

    assert(state.handleKey('s') == KeyResult::Screenshot);

    assert(state.axis() == axisBefore);
    assert(state.sliceIndex() == sliceBefore);
    assert(state.activeVolume() == volumeBefore);
}

/// While the range prompt is open, 's' is text like any other printable
/// character -- it must not be hijacked into a screenshot mid-edit.
void testScreenshotKeyIsSwallowedByThePrompt()
{
    auto state = makeState();
    state.handleKey('r');
    assert(state.handleKey('s') == KeyResult::Changed);
    assert(state.editor().text() == "s");
}

void testSingleSliceAxisCannotMove()
{
    auto state = makeState(glm::ivec3(4, 4, 1));
    assert(state.sliceCount() == 1);
    assert(state.handleKey('j') == KeyResult::Ignored);
    assert(state.handleKey('k') == KeyResult::Ignored);
    assert(state.sliceIndex() == 0);
}

/// The active view as a row number, which is what the on-screen marker
/// needs: the grid draws views in --views order, not in viewIndex order.
void testActiveViewRowIsAnIndexIntoTheDisplayedViews()
{
    glm::ivec3 dims(64, 128, 96);
    ViewState state({dims}, {2, 0}, 'y', glm::ivec3(32, 64, 48), displaysFor(1));

    assert(state.activeViewRow() == 0); // coronal is listed first
    assert(state.handleKey('z') == KeyResult::Changed);
    assert(state.activeViewRow() == 1);
}

/// Up and down walk the displayed views. They are an alternative to x/y/z,
/// not a replacement: the letters jump straight to a plane, the arrows step
/// through whatever --views actually put on screen.
void testVerticalArrowsWalkTheDisplayedViews()
{
    auto state = makeState(); // views {0, 1, 2}, axial active
    assert(state.handleKey(kKeyDown) == KeyResult::Changed);
    assert(state.activeViewRow() == 1);
    assert(state.axis() == 'x');

    assert(state.handleKey(kKeyUp) == KeyResult::Changed);
    assert(state.activeViewRow() == 0);
    assert(state.axis() == 'z');
}

/// Both directions wrap: the rows are a ring, and stopping dead at the end
/// of a three-row grid is more annoying than coming back round.
void testVerticalArrowsWrap()
{
    auto state = makeState();
    assert(state.handleKey(kKeyUp) == KeyResult::Changed);
    assert(state.activeViewRow() == 2);
    assert(state.handleKey(kKeyDown) == KeyResult::Changed);
    assert(state.activeViewRow() == 0);
}

/// With one view on screen there is nowhere to step, so the arrows change
/// nothing rather than repainting an identical frame.
void testVerticalArrowsWithOneViewAreIgnored()
{
    glm::ivec3 dims(64, 128, 96);
    ViewState state({dims}, {0}, 'z', glm::ivec3(32, 64, 48), displaysFor(1));
    assert(state.handleKey(kKeyDown) == KeyResult::Ignored);
    assert(state.handleKey(kKeyUp) == KeyResult::Ignored);
}

/// Left and right select the column, the same as Tab and the digits.
void testHorizontalArrowsSelectTheVolume()
{
    auto state = makeTwoVolumeState();
    assert(state.handleKey(kKeyRight) == KeyResult::Changed);
    assert(state.activeVolume() == 1);

    assert(state.handleKey(kKeyLeft) == KeyResult::Changed);
    assert(state.activeVolume() == 0);

    // Wrapping, like Tab.
    assert(state.handleKey(kKeyLeft) == KeyResult::Changed);
    assert(state.activeVolume() == 1);
}

void testHorizontalArrowsWithOneVolumeAreIgnored()
{
    auto state = makeState();
    assert(state.handleKey(kKeyRight) == KeyResult::Ignored);
    assert(state.handleKey(kKeyLeft) == KeyResult::Ignored);
}

/// The prompt owns every key while it is open, arrows included: they must
/// not move the grid underneath what is being typed.
void testArrowsDoNotEscapeTheRangePrompt()
{
    auto state = makeTwoVolumeState();
    state.handleKey('r');
    state.handleKey(kKeyRight);
    state.handleKey(kKeyDown);

    assert(state.isEditing());
    assert(state.activeVolume() == 0);
    assert(state.activeViewRow() == 0);
}

/// A terminal resize carries no state change of its own -- the caller is
/// supposed to recompute the display box and rebuild the frame against
/// whatever ViewState already holds. All ViewState needs to do is say
/// "repaint", so the resize handler in Screen::readKey() can stay logic-free
/// like every other translated key.
void testResizeForcesARepaintWithoutChangingAnything()
{
    auto state = makeTwoVolumeState();
    state.handleKey('x');
    state.handleKey('j');
    state.handleKey('\t');

    int sliceBefore  = state.sliceIndex();
    char axisBefore  = state.axis();
    int volumeBefore = state.activeVolume();

    assert(state.handleKey(kKeyResize) == KeyResult::Changed);

    assert(state.sliceIndex() == sliceBefore);
    assert(state.axis() == axisBefore);
    assert(state.activeVolume() == volumeBefore);
}

/// A resize mid-prompt must not be swallowed as a typed character -- it is
/// not text, and it must not close or corrupt what the user is entering.
void testResizeDuringThePromptRepaintsWithoutTouchingTheText()
{
    auto state = makeTwoVolumeState();
    state.handleKey('r');
    for (char key : std::string("20 1"))
        state.handleKey(key);

    assert(state.handleKey(kKeyResize) == KeyResult::Changed);

    assert(state.isEditing());
    assert(state.editor().text() == "20 1");
}

void testConstructorClampsTheCursor()
{
    glm::ivec3 dims(4, 4, 4);
    ViewState state({dims}, {0, 1, 2}, 'z', glm::ivec3(99, -3, 99), displaysFor(1));
    assert(state.sliceIndexFor(0, 0) == 3);
    assert(state.sliceIndexFor(0, 1) == 3);
    assert(state.sliceIndexFor(0, 2) == 0);
}

} // namespace

int main()
{
    testViewsAndVolumeCount();
    testEachViewKeepsItsOwnSlice();
    testJAndKMoveTheActiveAxisOnly();
    testSliceNavigationClampsAtBothEnds();
    testAxisSwitchIsLosslessRoundTrip();
    testReselectingTheCurrentAxisIsIgnored();
    testAxisNotDisplayedIsIgnored();
    testActiveAxisFallsBackToADisplayedView();
    testSliceNavigationIsSynchronisedAcrossVolumes();
    testSliceCountComesFromTheFirstVolume();
    testTabCyclesTheActiveVolume();
    testTabWithOneVolumeIsIgnored();
    testDigitsSelectAVolumeDirectly();
    testColourMapCyclesOnTheActiveVolumeOnly();
    testColourMapCycleWraps();
    testRangePromptSwallowsNavigationKeys();
    testRangePromptCommitsToTheActiveVolume();
    testRangePromptCancelLeavesTheRangeAlone();
    testEscapeInThePromptDoesNotQuit();
    testBadRangeKeepsThePromptOpen();
    testPromptStartsFromTheVolumesCurrentRange();
    testQuitAndUnknownKeys();
    testScreenshotKeyDoesNotChangeState();
    testScreenshotKeyIsSwallowedByThePrompt();
    testSingleSliceAxisCannotMove();
    testConstructorClampsTheCursor();
    testActiveViewRowIsAnIndexIntoTheDisplayedViews();
    testVerticalArrowsWalkTheDisplayedViews();
    testVerticalArrowsWrap();
    testVerticalArrowsWithOneViewAreIgnored();
    testHorizontalArrowsSelectTheVolume();
    testHorizontalArrowsWithOneVolumeAreIgnored();
    testArrowsDoNotEscapeTheRangePrompt();
    testResizeForcesARepaintWithoutChangingAnything();
    testResizeDuringThePromptRepaintsWithoutTouchingTheText();
    return 0;
}
