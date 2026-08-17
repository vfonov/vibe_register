/// test_slice_selection.cpp — --slice argument parsing ("n" | "p%" | "mid")
/// and resolution to a concrete voxel index. See PLAN.md's CLI surface and
/// mriv/HANDOFF.md sec 6.

#include <cassert>

#include "cli/SliceSelection.hpp"

using namespace mriv::term;

namespace
{

void testParseMid()
{
    auto sel = parseSliceArg("mid");
    assert(sel.has_value());
    assert(sel->kind == SliceSelectionKind::Mid);
}

void testParseAbsolute()
{
    auto sel = parseSliceArg("42");
    assert(sel.has_value());
    assert(sel->kind == SliceSelectionKind::Absolute);
    assert(sel->value == 42.0);
}

void testParseNegativeAbsolute()
{
    // Not a valid form -- renderSlice() clamps but negative indices are not
    // a supported CLI spelling. Reject at parse time.
    auto sel = parseSliceArg("-5");
    assert(!sel.has_value());
}

void testParsePercent()
{
    auto sel = parseSliceArg("50%");
    assert(sel.has_value());
    assert(sel->kind == SliceSelectionKind::Percent);
    assert(sel->value == 50.0);
}

void testParsePercentFractional()
{
    auto sel = parseSliceArg("12.5%");
    assert(sel.has_value());
    assert(sel->kind == SliceSelectionKind::Percent);
    assert(sel->value > 12.49 && sel->value < 12.51);
}

void testParseGarbageRejected()
{
    assert(!parseSliceArg("").has_value());
    assert(!parseSliceArg("abc").has_value());
    assert(!parseSliceArg("12x").has_value());
    assert(!parseSliceArg("%").has_value());
}

void testResolveMid()
{
    SliceSelection sel{SliceSelectionKind::Mid, 0.0};
    assert(resolveSliceIndex(sel, 96) == 48);
    assert(resolveSliceIndex(sel, 1) == 0);
}

void testResolveAbsoluteInRange()
{
    SliceSelection sel{SliceSelectionKind::Absolute, 10.0};
    assert(resolveSliceIndex(sel, 96) == 10);
}

void testResolveAbsoluteClamped()
{
    SliceSelection sel{SliceSelectionKind::Absolute, 999.0};
    assert(resolveSliceIndex(sel, 96) == 95);
}

void testResolvePercent()
{
    // 50% of a 0-indexed 96-slice volume is slice 47 (round(0.5 * 95)).
    SliceSelection sel{SliceSelectionKind::Percent, 50.0};
    assert(resolveSliceIndex(sel, 96) == 47 || resolveSliceIndex(sel, 96) == 48);

    SliceSelection zero{SliceSelectionKind::Percent, 0.0};
    assert(resolveSliceIndex(zero, 96) == 0);

    SliceSelection hundred{SliceSelectionKind::Percent, 100.0};
    assert(resolveSliceIndex(hundred, 96) == 95);
}

void testResolvePercentClamped()
{
    SliceSelection over{SliceSelectionKind::Percent, 150.0};
    assert(resolveSliceIndex(over, 96) == 95);

    SliceSelection under{SliceSelectionKind::Percent, -10.0};
    assert(resolveSliceIndex(under, 96) == 0);
}

} // namespace

int main()
{
    testParseMid();
    testParseAbsolute();
    testParseNegativeAbsolute();
    testParsePercent();
    testParsePercentFractional();
    testParseGarbageRejected();
    testResolveMid();
    testResolveAbsoluteInRange();
    testResolveAbsoluteClamped();
    testResolvePercent();
    testResolvePercentClamped();
    return 0;
}
