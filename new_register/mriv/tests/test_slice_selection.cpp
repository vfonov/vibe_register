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

// --- per-axis --slice spec ------------------------------------------------

void testSpecBareFormSetsActiveOnly()
{
    auto spec = parseSliceSpec("42");
    assert(spec.has_value());
    assert(spec->active.has_value());
    assert(spec->active->kind == SliceSelectionKind::Absolute);
    assert(spec->active->value == 42.0);
    assert(!spec->x.has_value());
    assert(!spec->y.has_value());
    assert(!spec->z.has_value());
}

void testSpecBareMidAndPercentStillWork()
{
    assert(parseSliceSpec("mid")->active->kind == SliceSelectionKind::Mid);
    assert(parseSliceSpec("50%")->active->kind == SliceSelectionKind::Percent);
}

void testSpecSingleAxisSetsOnlyThatAxis()
{
    auto spec = parseSliceSpec("x=10");
    assert(spec.has_value());
    assert(!spec->active.has_value());
    assert(spec->x.has_value());
    assert(spec->x->kind == SliceSelectionKind::Absolute);
    assert(spec->x->value == 10.0);
    assert(!spec->y.has_value());
    assert(!spec->z.has_value());
}

void testSpecAllThreeAxesInAnyOrder()
{
    auto spec = parseSliceSpec("z=30%,x=10,y=mid");
    assert(spec.has_value());
    assert(!spec->active.has_value());
    assert(spec->x.has_value() && spec->x->kind == SliceSelectionKind::Absolute
           && spec->x->value == 10.0);
    assert(spec->y.has_value() && spec->y->kind == SliceSelectionKind::Mid);
    assert(spec->z.has_value() && spec->z->kind == SliceSelectionKind::Percent
           && spec->z->value == 30.0);
}

void testSpecSubsetOfAxesLeavesOthersUnset()
{
    auto spec = parseSliceSpec("y=5");
    assert(spec.has_value());
    assert(spec->y.has_value());
    assert(!spec->x.has_value());
    assert(!spec->z.has_value());
}

void testSpecRejectsUnknownAxisLetter()
{
    assert(!parseSliceSpec("w=10").has_value());
}

void testSpecRejectsDuplicateAxis()
{
    assert(!parseSliceSpec("x=10,x=20").has_value());
}

void testSpecRejectsMixingBareAndAxisForms()
{
    assert(!parseSliceSpec("10,x=20").has_value());
    assert(!parseSliceSpec("x=20,10").has_value());
}

void testSpecRejectsMalformedAxisValue()
{
    assert(!parseSliceSpec("x=abc").has_value());
    assert(!parseSliceSpec("x=").has_value());
    assert(!parseSliceSpec("=10").has_value());
}

void testSpecRejectsEmpty()
{
    assert(!parseSliceSpec("").has_value());
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

    testSpecBareFormSetsActiveOnly();
    testSpecBareMidAndPercentStillWork();
    testSpecSingleAxisSetsOnlyThatAxis();
    testSpecAllThreeAxesInAnyOrder();
    testSpecSubsetOfAxesLeavesOthersUnset();
    testSpecRejectsUnknownAxisLetter();
    testSpecRejectsDuplicateAxis();
    testSpecRejectsMixingBareAndAxisForms();
    testSpecRejectsMalformedAxisValue();
    testSpecRejectsEmpty();
    return 0;
}
