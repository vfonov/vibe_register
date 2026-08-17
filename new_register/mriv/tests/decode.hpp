#pragma once

/// Decode helpers for Layer B pixel-correctness tests. Given a Kitty
/// graphics escape event produced by the mriv encoder, decode the
/// base64 PNG payload back into an RGBA buffer.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "escapes.hpp"

namespace mriv::term::test
{

struct DecodedImage
{
    std::vector<std::uint8_t> rgba; // row-major, width*height*4 bytes
    int width  = 0;
    int height = 0;
};

/// Decode the payload of a KittyGraphics event as PNG. Empty on failure.
DecodedImage decodeKittyEvent(const EscapeEvent& event);

} // namespace mriv::term::test
