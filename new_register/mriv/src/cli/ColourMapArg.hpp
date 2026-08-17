#pragma once

#include <optional>
#include <string>

#include "ColourMap.h"

namespace mriv::term
{

/// Resolve a user-supplied --colourmap argument to a ColourMapType by
/// normalising both sides (lowercase, strip spaces/underscores/hyphens)
/// before comparing against colourMapName(). This lets "hotmetal",
/// "hot-metal", "hot_metal" and "Hot Metal" all resolve to
/// ColourMapType::HotMetal. See mriv/HANDOFF.md sec 3.4 -- colourMapName()
/// itself does an exact match and will not accept these variants.
std::optional<ColourMapType> resolveColourMapArg(const std::string& arg);

/// Build the full list of accepted --colourmap names (colourMapName() for
/// every entry in [0, colourMapCount())), for use in error messages.
std::string listColourMapNames();

} // namespace mriv::term
