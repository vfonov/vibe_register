#pragma once

#include <optional>
#include <string>
#include <vector>

namespace mriv::term
{

/// Parse a --views argument ("z", "z,x,y", "y,z") into the parent's
/// renderSlice() viewIndex convention, in the order given. Duplicates are
/// dropped, keeping the first occurrence's position. Returns std::nullopt
/// for an empty list, an empty element, or an unknown axis letter, so the
/// CLI layer can report a clean error instead of silently defaulting.
///
/// The axis letter -> viewIndex mapping is not repeated here: it comes from
/// viewIndexForAxis() in render/SliceGeometry.hpp. A second copy of that
/// table is exactly the trap mriv/HANDOFF.md sec 3.5 warns about.
std::optional<std::vector<int>> parseViewList(const std::string& arg);

} // namespace mriv::term
