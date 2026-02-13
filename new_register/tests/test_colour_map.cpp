#include "ColourMap.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>

/// Extract RGBA components from a packed 0xAABBGGRR value.
static void unpack(uint32_t c, int& r, int& g, int& b, int& a)
{
    r = (c >>  0) & 0xFF;
    g = (c >>  8) & 0xFF;
    b = (c >> 16) & 0xFF;
    a = (c >> 24) & 0xFF;
}

static int failures = 0;

static void check(bool cond, const char* msg, int line)
{
    if (!cond)
    {
        std::fprintf(stderr, "FAIL (line %d): %s\n", line, msg);
        ++failures;
    }
}

#define CHECK(cond, msg) check((cond), (msg), __LINE__)

int main()
{
    // 1. All colour maps should be nameable and have valid LUTs.
    for (int i = 0; i < colourMapCount(); ++i)
    {
        auto type = static_cast<ColourMapType>(i);
        auto name = colourMapName(type);
        CHECK(!name.empty(), "colour map name should not be empty");

        const ColourLut& lut = colourMapLut(type);
        // All entries should have full alpha.
        for (int j = 0; j < kLutSize; ++j)
        {
            int r, g, b, a;
            unpack(lut.table[j], r, g, b, a);
            CHECK(a == 255, "alpha should be 255 for all built-in maps");
        }
    }

    // 2. Gray scale: first entry should be black, last should be white.
    {
        const ColourLut& lut = colourMapLut(ColourMapType::GrayScale);
        int r, g, b, a;
        unpack(lut.table[0], r, g, b, a);
        CHECK(r == 0 && g == 0 && b == 0, "gray LUT[0] should be black");

        unpack(lut.table[255], r, g, b, a);
        CHECK(r == 255 && g == 255 && b == 255, "gray LUT[255] should be white");

        // Midpoint should be ~128.
        unpack(lut.table[128], r, g, b, a);
        CHECK(std::abs(r - 128) <= 1, "gray LUT[128] R should be ~128");
        CHECK(r == g && g == b, "gray LUT[128] should be neutral");
    }

    // 3. Hot metal: first entry black, last entry white.
    {
        const ColourLut& lut = colourMapLut(ColourMapType::HotMetal);
        int r, g, b, a;
        unpack(lut.table[0], r, g, b, a);
        CHECK(r == 0 && g == 0 && b == 0, "hot LUT[0] should be black");

        unpack(lut.table[255], r, g, b, a);
        CHECK(r == 255 && g == 255 && b == 255, "hot LUT[255] should be white");

        // At 25% (~index 64) should be dark red: R~128, G~0, B~0.
        unpack(lut.table[64], r, g, b, a);
        CHECK(std::abs(r - 128) <= 2, "hot LUT[64] R should be ~128");
        CHECK(g <= 2, "hot LUT[64] G should be ~0");
        CHECK(b <= 2, "hot LUT[64] B should be ~0");
    }

    // 4. Red: first black, last pure red.
    {
        const ColourLut& lut = colourMapLut(ColourMapType::Red);
        int r, g, b, a;
        unpack(lut.table[0], r, g, b, a);
        CHECK(r == 0 && g == 0 && b == 0, "red LUT[0] should be black");

        unpack(lut.table[255], r, g, b, a);
        CHECK(r == 255, "red LUT[255] R should be 255");
        CHECK(g == 0 && b == 0, "red LUT[255] G,B should be 0");
    }

    // 5. Green: first black, last pure green.
    {
        const ColourLut& lut = colourMapLut(ColourMapType::Green);
        int r, g, b, a;
        unpack(lut.table[255], r, g, b, a);
        CHECK(r == 0 && g == 255 && b == 0, "green LUT[255] should be pure green");
    }

    // 6. Blue: first black, last pure blue.
    {
        const ColourLut& lut = colourMapLut(ColourMapType::Blue);
        int r, g, b, a;
        unpack(lut.table[255], r, g, b, a);
        CHECK(r == 0 && g == 0 && b == 255, "blue LUT[255] should be pure blue");
    }

    // 7. Spectral: first black, last light gray.
    {
        const ColourLut& lut = colourMapLut(ColourMapType::Spectral);
        int r, g, b, a;
        unpack(lut.table[0], r, g, b, a);
        CHECK(r == 0 && g == 0 && b == 0, "spectral LUT[0] should be black");

        unpack(lut.table[255], r, g, b, a);
        // Last control point is (0.8, 0.8, 0.8) -> 204
        CHECK(std::abs(r - 204) <= 1, "spectral LUT[255] R should be ~204");
        CHECK(r == g && g == b, "spectral LUT[255] should be neutral gray");
    }

    // 8. Hot Metal Neg: first white, last black (reversed).
    {
        const ColourLut& lut = colourMapLut(ColourMapType::HotMetalNeg);
        int r, g, b, a;
        unpack(lut.table[0], r, g, b, a);
        CHECK(r == 255 && g == 255 && b == 255, "hot neg LUT[0] should be white");

        unpack(lut.table[255], r, g, b, a);
        CHECK(r == 0 && g == 0 && b == 0, "hot neg LUT[255] should be black");
    }

    // 9. Contour: should have visible discontinuities.
    {
        const ColourLut& lut = colourMapLut(ColourMapType::Contour);
        // At the 0.166 boundary (~index 42), there should be a colour jump.
        int r1, g1, b1, a1, r2, g2, b2, a2;
        // Index 42 = 0.1647 (just before boundary) — should be blue-ish.
        unpack(lut.table[42], r1, g1, b1, a1);
        // Index 43 = 0.1686 (just after boundary) — should be cyan-ish.
        unpack(lut.table[43], r2, g2, b2, a2);
        // There should be a large jump in at least one channel.
        int maxDiff = std::max({std::abs(r2 - r1), std::abs(g2 - g1),
                                std::abs(b2 - b1)});
        CHECK(maxDiff > 30, "contour should have visible discontinuity near index 42-43");
    }

    // 10. colourMapCount() should equal the number of types.
    CHECK(colourMapCount() == 18, "should have 18 colour map types");

    // 11. Calling colourMapLut twice should return the same object (cached).
    {
        const ColourLut& a = colourMapLut(ColourMapType::Spectral);
        const ColourLut& b = colourMapLut(ColourMapType::Spectral);
        CHECK(&a == &b, "colourMapLut should return cached reference");
    }

    if (failures == 0)
    {
        std::printf("All colour map tests passed.\n");
        return 0;
    }
    else
    {
        std::fprintf(stderr, "%d test(s) failed.\n", failures);
        return 1;
    }
}
