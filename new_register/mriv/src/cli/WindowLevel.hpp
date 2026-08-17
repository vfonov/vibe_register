#pragma once

namespace mriv::term
{

/// Value range produced from --window/--level sugar over the parent's
/// valueMin/valueMax model (VolumeRenderParams).
struct ValueRange
{
    double valueMin;
    double valueMax;
};

/// Convert a window/level pair to a valueMin/valueMax range:
/// valueMin = level - window/2, valueMax = level + window/2.
/// See PLAN.md's CLI surface -- -W/-L are sugar, not a separate code path.
ValueRange windowLevelToRange(double window, double level);

} // namespace mriv::term
