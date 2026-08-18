#pragma once

#include <vector>

#include "interactive/ViewState.hpp"
#include "render/FrameBuilder.hpp"

class Volume;

namespace mriv::term
{

/// Turn the session model plus the loaded volumes into a frame request.
///
/// This is the single place where "what the state says" becomes "what gets
/// drawn", and both the one-shot path and the interactive loop go through
/// it -- the same arguments must produce the same layout piped to a file as
/// on a terminal. `volumes` is parallel to the state's volumes; a null
/// entry leaves that column blank.
FrameRequest planFrame(const ViewState& state,
                       const std::vector<const Volume*>& volumes,
                       int boxWidth,
                       int boxHeight,
                       int scale);

} // namespace mriv::term
