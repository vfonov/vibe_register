/// test_view_state.cpp — the interactive mode's key handling, in isolation.
///
/// ViewState is deliberately free of notcurses, Volume and IO so that all of
/// M5's navigation logic is testable on a host with no TTY and no pixel
/// protocol (mriv/HANDOFF.md sec 3.9). Everything asserted here is behaviour
/// a user can observe by pressing a key; the untestable part is confined to
/// interactive/Screen.

#include <cassert>

#include "interactive/ViewState.hpp"

using namespace mriv::term;

namespace
{

// The thick-slices fixture's shape, reused here purely because its three
// dimensions are all different -- a state machine that confuses X, Y and Z
// cannot hide behind a cube.
const glm::ivec3 kDims{64, 229, 96};

ViewState makeState(char axis = 'z')
{
    return ViewState(kDims, axis, sliceCountForView(*viewIndexForAxis(axis), kDims) / 2, 0.0, 100.0);
}

/// The displayed slice index is the cursor component belonging to the
/// current axis, so the same cursor shows a different index per axis.
void testSliceIndexFollowsAxis()
{
    ViewState state = makeState('z');
    assert(state.sliceCount() == 96);
    assert(state.sliceIndex() == 48);

    assert(state.handleKey('x') == KeyResult::Changed);
    assert(state.sliceCount() == 64);
    assert(state.sliceIndex() == 32);

    assert(state.handleKey('y') == KeyResult::Changed);
    assert(state.sliceCount() == 229);
    assert(state.sliceIndex() == 114);
}

void testJAndKMoveOneSlice()
{
    ViewState state = makeState('z');

    assert(state.handleKey('j') == KeyResult::Changed);
    assert(state.sliceIndex() == 49);
    assert(state.handleKey('j') == KeyResult::Changed);
    assert(state.sliceIndex() == 50);
    assert(state.handleKey('k') == KeyResult::Changed);
    assert(state.sliceIndex() == 49);
}

/// At either end of the volume the key is a no-op, and reports Ignored so
/// the caller does not repaint an identical frame.
void testSliceNavigationClampsAtBothEnds()
{
    ViewState top(kDims, 'z', 95, 0.0, 100.0);
    assert(top.sliceIndex() == 95);
    assert(top.handleKey('j') == KeyResult::Ignored);
    assert(top.sliceIndex() == 95);

    ViewState bottom(kDims, 'z', 0, 0.0, 100.0);
    assert(bottom.handleKey('k') == KeyResult::Ignored);
    assert(bottom.sliceIndex() == 0);
}

void testConstructorClampsSliceIndex()
{
    ViewState high(kDims, 'z', 9999, 0.0, 100.0);
    assert(high.sliceIndex() == 95);

    ViewState low(kDims, 'z', -5, 0.0, 100.0);
    assert(low.sliceIndex() == 0);
}

/// Axis switching moves a 3D cursor rather than rescaling one index, so
/// leaving an axis and coming back lands exactly where you left it. The
/// axes are geometrically independent -- there is no meaningful way to map
/// "60% along Z" onto X -- which is why each axis keeps its own position.
void testAxisSwitchIsLosslessRoundTrip()
{
    ViewState state = makeState('z');
    state.handleKey('j');
    state.handleKey('j');
    state.handleKey('j');
    assert(state.sliceIndex() == 51);

    assert(state.handleKey('x') == KeyResult::Changed);
    assert(state.sliceIndex() == 32);
    state.handleKey('j');
    assert(state.sliceIndex() == 33);

    assert(state.handleKey('z') == KeyResult::Changed);
    assert(state.axis() == 'z');
    assert(state.sliceIndex() == 51);

    assert(state.handleKey('x') == KeyResult::Changed);
    assert(state.sliceIndex() == 33);
}

void testReselectingTheCurrentAxisIsIgnored()
{
    ViewState state = makeState('z');
    assert(state.handleKey('z') == KeyResult::Ignored);
    assert(state.axis() == 'z');
    assert(state.sliceIndex() == 48);
}

/// '+' widens the intensity window (less contrast), '-' narrows it (more
/// contrast), both about the window's centre so the displayed midtone does
/// not drift as the user adjusts it.
void testWindowKeysScaleTheRangeAboutItsCentre()
{
    ViewState state(kDims, 'z', 0, 100.0, 200.0);

    assert(state.handleKey('+') == KeyResult::Changed);
    assert(state.rangeLow() < 100.0 && state.rangeHigh() > 200.0);
    double centre = (state.rangeLow() + state.rangeHigh()) / 2.0;
    assert(centre > 149.999 && centre < 150.001);
    double widened = state.rangeHigh() - state.rangeLow();
    assert(widened > 100.0);

    assert(state.handleKey('-') == KeyResult::Changed);
    double restored = state.rangeHigh() - state.rangeLow();
    assert(restored > 99.999 && restored < 100.001);
    centre = (state.rangeLow() + state.rangeHigh()) / 2.0;
    assert(centre > 149.999 && centre < 150.001);
}

/// Scaling is multiplicative, so a window can be narrowed indefinitely
/// without ever collapsing to zero width or inverting.
void testNarrowingNeverInvertsTheWindow()
{
    ViewState state(kDims, 'z', 0, 0.0, 1.0);
    for (int i = 0; i < 200; ++i)
        state.handleKey('-');
    assert(state.rangeLow() < state.rangeHigh());
}

/// A volume whose auto-window quantiles coincide (a constant image) has no
/// window to scale; the keys report Ignored rather than producing an
/// inverted or NaN range.
void testWindowKeysOnADegenerateRangeAreIgnored()
{
    ViewState state(kDims, 'z', 0, 7.0, 7.0);
    assert(state.handleKey('+') == KeyResult::Ignored);
    assert(state.handleKey('-') == KeyResult::Ignored);
    assert(state.rangeLow() == 7.0 && state.rangeHigh() == 7.0);
}

void testQuitAndUnknownKeys()
{
    ViewState state = makeState('z');
    assert(state.handleKey('q') == KeyResult::Quit);

    // No 'h'/'l': there is no time axis to navigate (PLAN.md, deferred work).
    assert(state.handleKey('h') == KeyResult::Ignored);
    assert(state.handleKey('l') == KeyResult::Ignored);
    assert(state.handleKey('Z') == KeyResult::Ignored);
    assert(state.handleKey('\0') == KeyResult::Ignored);
}

/// A degenerate single-slice axis must not let navigation escape the volume.
void testSingleSliceAxis()
{
    ViewState state(glm::ivec3{1, 1, 1}, 'z', 0, 0.0, 1.0);
    assert(state.sliceCount() == 1);
    assert(state.handleKey('j') == KeyResult::Ignored);
    assert(state.handleKey('k') == KeyResult::Ignored);
    assert(state.sliceIndex() == 0);
}

} // namespace

int main()
{
    testSliceIndexFollowsAxis();
    testJAndKMoveOneSlice();
    testSliceNavigationClampsAtBothEnds();
    testConstructorClampsSliceIndex();
    testAxisSwitchIsLosslessRoundTrip();
    testReselectingTheCurrentAxisIsIgnored();
    testWindowKeysScaleTheRangeAboutItsCentre();
    testNarrowingNeverInvertsTheWindow();
    testWindowKeysOnADegenerateRangeAreIgnored();
    testQuitAndUnknownKeys();
    testSingleSliceAxis();

    return 0;
}
