#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "render/PixelProtocol.hpp"

namespace mriv::term
{

/// Encode a packed RGBA (0xAABBGGRR) buffer of size (w,h) into the
/// chosen terminal graphics protocol bytes. The returned string is the
/// raw escape-sequence payload that a compatible terminal would decode.
///
/// - Kitty: `a=T,f=100,s=W,v=H;` followed by base64-encoded PNG bytes,
///   terminated with `\x1b\\`.
/// - Sixel / iTerm2: currently returns empty (force Kitty for tests).
std::string encodePixels(
    PixelProtocol protocol,
    const std::uint8_t* rgba,
    std::size_t width,
    std::size_t height);

/// Encode a buffer as the Kitty graphics payload (PNG, format 100).
/// Exposed separately so tests can inspect the framing without going
/// through the full terminal layer.
std::string encodeKittyPng(const std::uint8_t* rgba,
                           std::size_t width,
                           std::size_t height);

} // namespace mriv::term
