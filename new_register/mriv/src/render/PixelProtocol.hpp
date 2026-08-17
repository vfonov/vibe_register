#pragma once

#include <cstdint>

namespace mriv::term
{

/// The terminal pixel-graphics protocols mriv can emit. In production,
/// notcurses selects one based on terminal capabilities; in tests we
/// force a specific protocol so escape-sequence assertions are stable.
enum class PixelProtocol : std::uint8_t
{
    None = 0,
    Kitty,
    Sixel,
    ITerm2,
};

} // namespace mriv::term
