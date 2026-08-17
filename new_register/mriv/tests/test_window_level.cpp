/// test_window_level.cpp — -W/-L sugar over VolumeRenderParams'
/// valueMin/valueMax. Three lines of arithmetic (PLAN.md), and exactly the
/// kind of thing that gets sign-flipped -- test it directly.

#include <cassert>

#include "cli/WindowLevel.hpp"

using namespace mriv::term;

namespace
{

void testSymmetricRange()
{
    // window=100, level=50 -> [0, 100]
    auto range = windowLevelToRange(100.0, 50.0);
    assert(range.valueMin == 0.0);
    assert(range.valueMax == 100.0);
}

void testZeroLevel()
{
    // window=10, level=0 -> [-5, 5]
    auto range = windowLevelToRange(10.0, 0.0);
    assert(range.valueMin == -5.0);
    assert(range.valueMax == 5.0);
}

void testNegativeLevel()
{
    // window=20, level=-10 -> [-20, 0]
    auto range = windowLevelToRange(20.0, -10.0);
    assert(range.valueMin == -20.0);
    assert(range.valueMax == 0.0);
}

void testFractionalWindow()
{
    auto range = windowLevelToRange(1.0, 0.5);
    assert(range.valueMin == 0.0);
    assert(range.valueMax == 1.0);
}

} // namespace

int main()
{
    testSymmetricRange();
    testZeroLevel();
    testNegativeLevel();
    testFractionalWindow();
    return 0;
}
