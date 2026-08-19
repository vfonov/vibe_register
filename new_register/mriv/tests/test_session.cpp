/// test_session.cpp — the interactive render loop.
///
/// The loop is parameterised over a key source and a frame sink so it can be
/// driven from a scripted list of keypresses with no terminal involved. What
/// is tested here is the loop's contract: when it repaints, when it stops,
/// and what it returns. The notcurses half lives in interactive/Screen and is
/// deliberately kept too thin to hold logic (mriv/HANDOFF.md sec 3.9).

#include <cassert>
#include <string>
#include <vector>

#include "interactive/Session.hpp"

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

const glm::ivec3 kDims{64, 229, 96};

/// Feeds a scripted key sequence, then reports end-of-input.
struct ScriptedKeys
{
    std::string keys;
    size_t next = 0;
    int calls = 0;

    std::optional<char> operator()()
    {
        ++calls;
        if (next >= keys.size())
            return std::nullopt;
        return keys[next++];
    }
};

/// Records what each frame would have shown.
struct RecordingSink
{
    struct Frame
    {
        char axis;
        int sliceIndex;
        double rangeLow;
        double rangeHigh;
        ColourMapType colourMap;
    };

    std::vector<Frame> frames;
    int failAtCall = -1;

    bool operator()(const ViewState& state)
    {
        if (failAtCall >= 0 && static_cast<int>(frames.size()) == failAtCall)
            return false;
        frames.push_back({state.axis(), state.sliceIndex(),
                          state.display(0).rangeLow, state.display(0).rangeHigh,
                          state.display(0).colourMap});
        return true;
    }
};

ViewState makeState()
{
    return makeViewState(kDims, 'z', 48, 0.0, 100.0);
}

/// The user must see the slice they asked for before pressing anything.
void testInitialFrameIsDrawnBeforeAnyKeyIsRead()
{
    ViewState state = makeState();
    ScriptedKeys keys{"q"};
    RecordingSink sink;

    assert(runSession(state, std::ref(keys), std::ref(sink)) == 0);
    assert(!sink.frames.empty());
    assert(sink.frames.front().axis == 'z');
    assert(sink.frames.front().sliceIndex == 48);
}

void testQuitKeyEndsTheSession()
{
    ViewState state = makeState();
    ScriptedKeys keys{"jq"};
    RecordingSink sink;

    assert(runSession(state, std::ref(keys), std::ref(sink)) == 0);
    // Initial frame + one for 'j'. 'q' must not draw again.
    assert(sink.frames.size() == 2);
    // 'q' was read, but nothing after it.
    assert(keys.next == 2);
}

/// A closed input (EOF, or the terminal going away) is a clean exit, not an
/// error -- it is what a piped or backgrounded invocation looks like.
void testEndOfInputEndsTheSessionCleanly()
{
    ViewState state = makeState();
    ScriptedKeys keys{"jj"};
    RecordingSink sink;

    assert(runSession(state, std::ref(keys), std::ref(sink)) == 0);
    assert(sink.frames.size() == 3);
    assert(sink.frames.back().sliceIndex == 50);
}

/// Repainting costs a full slice render and a bitmap upload, so a key that
/// changed nothing must not trigger one.
void testOnlyChangingKeysRepaint()
{
    ViewState state = makeState();
    // 'z' is already the axis, 'w' is unbound, and 'k' does change things.
    ScriptedKeys keys{"zwk"};
    RecordingSink sink;

    assert(runSession(state, std::ref(keys), std::ref(sink)) == 0);
    assert(sink.frames.size() == 2);
    assert(sink.frames.back().sliceIndex == 47);
}

/// Clamping at the end of the volume produces no repaint either, so holding
/// 'j' at the top of a stack is free rather than a redraw storm.
void testClampedNavigationDoesNotRepaint()
{
    ViewState state = makeViewState(kDims, 'z', 95, 0.0, 100.0);
    ScriptedKeys keys{"jjj"};
    RecordingSink sink;

    assert(runSession(state, std::ref(keys), std::ref(sink)) == 0);
    assert(sink.frames.size() == 1);
}

void testFramesFollowTheKeySequence()
{
    ViewState state = makeState();
    ScriptedKeys keys{"jxk"};
    RecordingSink sink;

    assert(runSession(state, std::ref(keys), std::ref(sink)) == 0);
    assert(sink.frames.size() == 4);
    assert(sink.frames[0].axis == 'z' && sink.frames[0].sliceIndex == 48);
    assert(sink.frames[1].axis == 'z' && sink.frames[1].sliceIndex == 49);
    assert(sink.frames[2].axis == 'x' && sink.frames[2].sliceIndex == 32);
    assert(sink.frames[3].axis == 'x' && sink.frames[3].sliceIndex == 31);
}

/// A display change repaints just like a navigation change: the loop must
/// not treat "the cursor did not move" as "nothing to redraw".
void testDisplayKeysReachTheSink()
{
    ViewState state = makeState();
    ScriptedKeys keys{"c"};
    RecordingSink sink;

    assert(runSession(state, std::ref(keys), std::ref(sink)) == 0);
    assert(sink.frames.size() == 2);
    assert(sink.frames[1].sliceIndex == sink.frames[0].sliceIndex);
    assert(sink.frames[1].colourMap != sink.frames[0].colourMap);
}

/// A sink that cannot draw ends the session non-zero and stops immediately;
/// continuing would spin on a terminal that is no longer accepting frames.
void testFailedFrameEndsTheSessionNonZero()
{
    ViewState state = makeState();
    ScriptedKeys keys{"jjjj"};
    RecordingSink sink;
    sink.failAtCall = 2; // initial frame and the first 'j' draw, then fail

    assert(runSession(state, std::ref(keys), std::ref(sink)) == 1);
    assert(sink.frames.size() == 2);
    assert(keys.calls == 2);
}

/// 's' must call the screenshot sink, not the frame sink -- nothing about
/// the view changed, so a repaint would be wasted work.
void testScreenshotKeyCallsTheScreenshotSinkNotTheFrameSink()
{
    ViewState state = makeState();
    ScriptedKeys keys{"sq"};
    RecordingSink sink;
    int screenshots = 0;

    assert(runSession(state, std::ref(keys), std::ref(sink),
                      [&]() { ++screenshots; }) == 0);
    assert(screenshots == 1);
    // Initial frame only -- 's' did not trigger a second draw.
    assert(sink.frames.size() == 1);
}

/// The session keeps going after a screenshot: it is a side effect, not an
/// exit.
void testSessionContinuesAfterAScreenshot()
{
    ViewState state = makeState();
    ScriptedKeys keys{"sjq"};
    RecordingSink sink;
    int screenshots = 0;

    assert(runSession(state, std::ref(keys), std::ref(sink),
                      [&]() { ++screenshots; }) == 0);
    assert(screenshots == 1);
    // Initial frame + one for 'j'; 's' and 'q' draw nothing.
    assert(sink.frames.size() == 2);
    assert(sink.frames.back().sliceIndex == 49);
}

/// A caller that does not pass a screenshot sink (most tests, and any
/// future caller that genuinely has nothing to save) must not crash on
/// 's' -- the default is a no-op, not an unset std::function call.
void testScreenshotKeyWithNoSinkDoesNotCrash()
{
    ViewState state = makeState();
    ScriptedKeys keys{"sq"};
    RecordingSink sink;

    assert(runSession(state, std::ref(keys), std::ref(sink)) == 0);
}

void testFailedInitialFrameReadsNoKeys()
{
    ViewState state = makeState();
    ScriptedKeys keys{"jjjj"};
    RecordingSink sink;
    sink.failAtCall = 0;

    assert(runSession(state, std::ref(keys), std::ref(sink)) == 1);
    assert(sink.frames.empty());
    assert(keys.calls == 0);
}

} // namespace

int main()
{
    testInitialFrameIsDrawnBeforeAnyKeyIsRead();
    testQuitKeyEndsTheSession();
    testEndOfInputEndsTheSessionCleanly();
    testOnlyChangingKeysRepaint();
    testClampedNavigationDoesNotRepaint();
    testFramesFollowTheKeySequence();
    testDisplayKeysReachTheSink();
    testFailedFrameEndsTheSessionNonZero();
    testScreenshotKeyCallsTheScreenshotSinkNotTheFrameSink();
    testSessionContinuesAfterAScreenshot();
    testScreenshotKeyWithNoSinkDoesNotCrash();
    testFailedInitialFrameReadsNoKeys();

    return 0;
}
