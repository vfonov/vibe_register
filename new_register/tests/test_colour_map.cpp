#include "ColourMap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

namespace
{

struct Rgba
{
    int r, g, b, a;
};

/// Extract RGBA components from a packed 0xAABBGGRR value.
Rgba unpack(uint32_t c)
{
    return { static_cast<int>((c >>  0) & 0xFF),
             static_cast<int>((c >>  8) & 0xFF),
             static_cast<int>((c >> 16) & 0xFF),
             static_cast<int>((c >> 24) & 0xFF) };
}

} // namespace

TEST_CASE("all colour maps are named and fully opaque", "[colourmap]")
{
    for (int i = 0; i < colourMapCount(); ++i)
    {
        auto type = static_cast<ColourMapType>(i);
        CAPTURE(i);
        CHECK_FALSE(colourMapName(type).empty());

        const ColourLut& lut = colourMapLut(type);
        for (int j = 0; j < kLutSize; ++j)
        {
            CAPTURE(j);
            CHECK(unpack(lut.table[j]).a == 255);
        }
    }
}

TEST_CASE("gray scale runs from black to white", "[colourmap]")
{
    const ColourLut& lut = colourMapLut(ColourMapType::GrayScale);

    Rgba c = unpack(lut.table[0]);
    CHECK(c.r == 0);
    CHECK(c.g == 0);
    CHECK(c.b == 0);

    c = unpack(lut.table[255]);
    CHECK(c.r == 255);
    CHECK(c.g == 255);
    CHECK(c.b == 255);

    // Midpoint should be ~128 and neutral.
    c = unpack(lut.table[128]);
    CHECK(std::abs(c.r - 128) <= 1);
    CHECK(c.r == c.g);
    CHECK(c.g == c.b);
}

TEST_CASE("hot metal runs from black through dark red to white", "[colourmap]")
{
    const ColourLut& lut = colourMapLut(ColourMapType::HotMetal);

    Rgba c = unpack(lut.table[0]);
    CHECK(c.r == 0);
    CHECK(c.g == 0);
    CHECK(c.b == 0);

    c = unpack(lut.table[255]);
    CHECK(c.r == 255);
    CHECK(c.g == 255);
    CHECK(c.b == 255);

    // At 25% (~index 64) should be dark red: R~128, G~0, B~0.
    c = unpack(lut.table[64]);
    CHECK(std::abs(c.r - 128) <= 2);
    CHECK(c.g <= 2);
    CHECK(c.b <= 2);
}

TEST_CASE("invertColourLut reverses the LUT", "[colourmap]")
{
    const ColourLut& original = colourMapLut(ColourMapType::HotMetal);
    ColourLut inverted = invertColourLut(original);

    // First entry of inverted should match last entry of original.
    Rgba c = unpack(inverted.table[0]);
    CHECK(c.r == 255);
    CHECK(c.g == 255);
    CHECK(c.b == 255);

    // Last entry of inverted should match first entry of original.
    c = unpack(inverted.table[255]);
    CHECK(c.r == 0);
    CHECK(c.g == 0);
    CHECK(c.b == 0);

    // Symmetry: inverted[i] == original[255-i].
    for (int i = 0; i < kLutSize; ++i)
    {
        CAPTURE(i);
        CHECK(inverted.table[i] == original.table[kLutSize - 1 - i]);
    }
}

TEST_CASE("single-channel maps end in the pure primary", "[colourmap]")
{
    SECTION("red")
    {
        const ColourLut& lut = colourMapLut(ColourMapType::Red);
        Rgba c = unpack(lut.table[0]);
        CHECK(c.r == 0);
        CHECK(c.g == 0);
        CHECK(c.b == 0);

        c = unpack(lut.table[255]);
        CHECK(c.r == 255);
        CHECK(c.g == 0);
        CHECK(c.b == 0);
    }

    SECTION("green")
    {
        Rgba c = unpack(colourMapLut(ColourMapType::Green).table[255]);
        CHECK(c.r == 0);
        CHECK(c.g == 255);
        CHECK(c.b == 0);
    }

    SECTION("blue")
    {
        Rgba c = unpack(colourMapLut(ColourMapType::Blue).table[255]);
        CHECK(c.r == 0);
        CHECK(c.g == 0);
        CHECK(c.b == 255);
    }
}

TEST_CASE("spectral runs from black to light gray", "[colourmap]")
{
    const ColourLut& lut = colourMapLut(ColourMapType::Spectral);

    Rgba c = unpack(lut.table[0]);
    CHECK(c.r == 0);
    CHECK(c.g == 0);
    CHECK(c.b == 0);

    // Last control point is (0.8, 0.8, 0.8) -> 204.
    c = unpack(lut.table[255]);
    CHECK(std::abs(c.r - 204) <= 1);
    CHECK(c.r == c.g);
    CHECK(c.g == c.b);
}

TEST_CASE("contour has a visible discontinuity", "[colourmap]")
{
    const ColourLut& lut = colourMapLut(ColourMapType::Contour);
    // At the 0.166 boundary (~index 42), there should be a colour jump.
    // Index 42 = 0.1647 (just before boundary) — should be blue-ish.
    Rgba c1 = unpack(lut.table[42]);
    // Index 43 = 0.1686 (just after boundary) — should be cyan-ish.
    Rgba c2 = unpack(lut.table[43]);
    // There should be a large jump in at least one channel.
    int maxDiff = std::max({ std::abs(c2.r - c1.r), std::abs(c2.g - c1.g),
                             std::abs(c2.b - c1.b) });
    CHECK(maxDiff > 30);
}

TEST_CASE("colourMapCount matches the number of types", "[colourmap]")
{
    CHECK(colourMapCount() == 18);
}

TEST_CASE("colourMapLut returns a cached reference", "[colourmap]")
{
    const ColourLut& a = colourMapLut(ColourMapType::Spectral);
    const ColourLut& b = colourMapLut(ColourMapType::Spectral);
    CHECK(&a == &b);
}

TEST_CASE("colourMapByName looks up display names", "[colourmap]")
{
    SECTION("valid names")
    {
        CHECK(colourMapByName("Gray") == ColourMapType::GrayScale);
        CHECK(colourMapByName("Hot Metal") == ColourMapType::HotMetal);
        CHECK(colourMapByName("Spectral") == ColourMapType::Spectral);
    }

    SECTION("invalid names return nullopt")
    {
        CHECK_FALSE(colourMapByName("NonExistent").has_value());
        CHECK_FALSE(colourMapByName("").has_value());
    }

    SECTION("name round-trips to type for every map")
    {
        for (int i = 0; i < colourMapCount(); ++i)
        {
            auto type = static_cast<ColourMapType>(i);
            CAPTURE(i);
            CHECK(colourMapByName(colourMapName(type)) == type);
        }
    }
}
