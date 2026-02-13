#pragma once

#include <array>
#include <cstdint>
#include <string_view>

/// Number of entries in each precomputed lookup table.
constexpr int kLutSize = 256;

/// Supported colour map types.  The order matches the legacy bicpl
/// Colour_coding_types enum for the maps we implement.
enum class ColourMapType
{
    GrayScale,
    HotMetal,
    HotMetalNeg,
    ColdMetal,
    ColdMetalNeg,
    GreenMetal,
    GreenMetalNeg,
    LimeMetal,
    LimeMetalNeg,
    RedMetal,
    RedMetalNeg,
    PurpleMetal,
    PurpleMetalNeg,
    Spectral,
    Red,
    Green,
    Blue,
    Contour,

    Count  // sentinel — must be last
};

/// A precomputed 256-entry RGBA lookup table.
/// Entry i corresponds to the normalised intensity i/255.
/// Each entry is packed as 0xAABBGGRR (Vulkan / ImGui byte order).
struct ColourLut
{
    std::array<uint32_t, kLutSize> table{};
};

/// Return the human-readable display name for a colour map type.
std::string_view colourMapName(ColourMapType type);

/// Build (or return cached) the lookup table for the given colour map.
/// The returned reference is valid for the lifetime of the program.
const ColourLut& colourMapLut(ColourMapType type);

/// Total number of colour map types (excluding the Count sentinel).
constexpr int colourMapCount()
{
    return static_cast<int>(ColourMapType::Count);
}
