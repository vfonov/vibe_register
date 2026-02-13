#include "ColourMap.h"

#include <algorithm>
#include <cmath>
#include <vector>

// -----------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------

namespace
{

/// A single control point in a piecewise-linear colour ramp.
struct ControlPoint
{
    float pos;          // normalised position [0, 1]
    float r, g, b, a;   // RGBA [0, 1]
};

/// Pack floating-point RGBA into 0xAABBGGRR (Vulkan / ImGui byte order).
inline uint32_t packRGBA(float r, float g, float b, float a)
{
    auto clamp01 = [](float v) -> float
    {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    };
    uint8_t ri = static_cast<uint8_t>(clamp01(r) * 255.0f + 0.5f);
    uint8_t gi = static_cast<uint8_t>(clamp01(g) * 255.0f + 0.5f);
    uint8_t bi = static_cast<uint8_t>(clamp01(b) * 255.0f + 0.5f);
    uint8_t ai = static_cast<uint8_t>(clamp01(a) * 255.0f + 0.5f);
    return static_cast<uint32_t>(ai) << 24 |
           static_cast<uint32_t>(bi) << 16 |
           static_cast<uint32_t>(gi) << 8  |
           static_cast<uint32_t>(ri);
}

/// Linearly interpolate between two control points, in RGB space.
uint32_t interpolate(const ControlPoint& p0, const ControlPoint& p1,
                     float pos)
{
    float span = p1.pos - p0.pos;
    float t = (span > 1e-9f) ? (pos - p0.pos) / span : 0.0f;
    float r = p0.r + (p1.r - p0.r) * t;
    float g = p0.g + (p1.g - p0.g) * t;
    float b = p0.b + (p1.b - p0.b) * t;
    float a = p0.a + (p1.a - p0.a) * t;
    return packRGBA(r, g, b, a);
}

/// Build a 256-entry LUT from piecewise-linear control points.
ColourLut buildFromControlPoints(const ControlPoint* pts, int nPts)
{
    ColourLut lut;
    int seg = 0;
    for (int i = 0; i < kLutSize; ++i)
    {
        float pos = static_cast<float>(i) / static_cast<float>(kLutSize - 1);

        // Advance to the correct segment.
        while (seg < nPts - 2 && pos > pts[seg + 1].pos)
            ++seg;

        lut.table[i] = interpolate(pts[seg], pts[seg + 1], pos);
    }
    return lut;
}

// -----------------------------------------------------------------------
// Control point definitions — matching legacy bicpl exactly.
// -----------------------------------------------------------------------

// --- Gray Scale ---
static const ControlPoint kGrayScale[] = {
    { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f },
    { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
};

// --- Hot Metal ---
static const ControlPoint kHotMetal[] = {
    { 0.00f, 0.0f, 0.0f, 0.0f, 1.0f },
    { 0.25f, 0.5f, 0.0f, 0.0f, 1.0f },
    { 0.50f, 1.0f, 0.5f, 0.0f, 1.0f },
    { 0.75f, 1.0f, 1.0f, 0.5f, 1.0f },
    { 1.00f, 1.0f, 1.0f, 1.0f, 1.0f },
};

// --- Hot Metal Negative ---
static const ControlPoint kHotMetalNeg[] = {
    { 0.00f, 1.0f, 1.0f, 1.0f, 1.0f },
    { 0.25f, 1.0f, 1.0f, 0.5f, 1.0f },
    { 0.50f, 1.0f, 0.5f, 0.0f, 1.0f },
    { 0.75f, 0.5f, 0.0f, 0.0f, 1.0f },
    { 1.00f, 0.0f, 0.0f, 0.0f, 1.0f },
};

// --- Cold Metal ---
static const ControlPoint kColdMetal[] = {
    { 0.00f, 0.0f, 0.0f, 0.0f, 1.0f },
    { 0.25f, 0.0f, 0.0f, 0.5f, 1.0f },
    { 0.50f, 0.0f, 0.5f, 1.0f, 1.0f },
    { 0.75f, 0.5f, 1.0f, 1.0f, 1.0f },
    { 1.00f, 1.0f, 1.0f, 1.0f, 1.0f },
};

// --- Cold Metal Negative ---
static const ControlPoint kColdMetalNeg[] = {
    { 0.00f, 1.0f, 1.0f, 1.0f, 1.0f },
    { 0.25f, 0.5f, 1.0f, 1.0f, 1.0f },
    { 0.50f, 0.0f, 0.5f, 1.0f, 1.0f },
    { 0.75f, 0.0f, 0.0f, 0.5f, 1.0f },
    { 1.00f, 0.0f, 0.0f, 0.0f, 1.0f },
};

// --- Green Metal ---
static const ControlPoint kGreenMetal[] = {
    { 0.00f, 0.0f, 0.0f, 0.0f, 1.0f },
    { 0.25f, 0.0f, 0.5f, 0.0f, 1.0f },
    { 0.50f, 0.0f, 1.0f, 0.5f, 1.0f },
    { 0.75f, 0.5f, 1.0f, 1.0f, 1.0f },
    { 1.00f, 1.0f, 1.0f, 1.0f, 1.0f },
};

// --- Green Metal Negative ---
static const ControlPoint kGreenMetalNeg[] = {
    { 0.00f, 1.0f, 1.0f, 1.0f, 1.0f },
    { 0.25f, 0.5f, 1.0f, 1.0f, 1.0f },
    { 0.50f, 0.0f, 1.0f, 0.5f, 1.0f },
    { 0.75f, 0.0f, 0.5f, 0.0f, 1.0f },
    { 1.00f, 0.0f, 0.0f, 0.0f, 1.0f },
};

// --- Lime Metal ---
static const ControlPoint kLimeMetal[] = {
    { 0.00f, 0.0f, 0.0f, 0.0f, 1.0f },
    { 0.25f, 0.0f, 0.5f, 0.0f, 1.0f },
    { 0.50f, 0.5f, 1.0f, 0.0f, 1.0f },
    { 0.75f, 1.0f, 1.0f, 0.5f, 1.0f },
    { 1.00f, 1.0f, 1.0f, 1.0f, 1.0f },
};

// --- Lime Metal Negative ---
static const ControlPoint kLimeMetalNeg[] = {
    { 0.00f, 1.0f, 1.0f, 1.0f, 1.0f },
    { 0.25f, 1.0f, 1.0f, 0.5f, 1.0f },
    { 0.50f, 0.5f, 1.0f, 0.0f, 1.0f },
    { 0.75f, 0.0f, 0.5f, 0.0f, 1.0f },
    { 1.00f, 0.0f, 0.0f, 0.0f, 1.0f },
};

// --- Red Metal ---
static const ControlPoint kRedMetal[] = {
    { 0.00f, 0.0f, 0.0f, 0.0f, 1.0f },
    { 0.25f, 0.5f, 0.0f, 0.0f, 1.0f },
    { 0.50f, 1.0f, 0.0f, 0.5f, 1.0f },
    { 0.75f, 1.0f, 0.5f, 1.0f, 1.0f },
    { 1.00f, 1.0f, 1.0f, 1.0f, 1.0f },
};

// --- Red Metal Negative ---
static const ControlPoint kRedMetalNeg[] = {
    { 0.00f, 1.0f, 1.0f, 1.0f, 1.0f },
    { 0.25f, 1.0f, 0.5f, 1.0f, 1.0f },
    { 0.50f, 1.0f, 0.0f, 0.5f, 1.0f },
    { 0.75f, 0.5f, 0.0f, 0.0f, 1.0f },
    { 1.00f, 0.0f, 0.0f, 0.0f, 1.0f },
};

// --- Purple Metal ---
static const ControlPoint kPurpleMetal[] = {
    { 0.00f, 0.0f, 0.0f, 0.0f, 1.0f },
    { 0.25f, 0.0f, 0.0f, 0.5f, 1.0f },
    { 0.50f, 0.5f, 0.0f, 1.0f, 1.0f },
    { 0.75f, 1.0f, 0.5f, 1.0f, 1.0f },
    { 1.00f, 1.0f, 1.0f, 1.0f, 1.0f },
};

// --- Purple Metal Negative ---
static const ControlPoint kPurpleMetalNeg[] = {
    { 0.00f, 1.0f, 1.0f, 1.0f, 1.0f },
    { 0.25f, 1.0f, 0.5f, 1.0f, 1.0f },
    { 0.50f, 0.5f, 0.0f, 1.0f, 1.0f },
    { 0.75f, 0.0f, 0.0f, 0.5f, 1.0f },
    { 1.00f, 0.0f, 0.0f, 0.0f, 1.0f },
};

// --- Spectral (21 control points) ---
static const ControlPoint kSpectral[] = {
    { 0.00f, 0.0000f, 0.0000f, 0.0000f, 1.0f },
    { 0.05f, 0.4667f, 0.0000f, 0.5333f, 1.0f },
    { 0.10f, 0.5333f, 0.0000f, 0.6000f, 1.0f },
    { 0.15f, 0.0000f, 0.0000f, 0.6667f, 1.0f },
    { 0.20f, 0.0000f, 0.0000f, 0.8667f, 1.0f },
    { 0.25f, 0.0000f, 0.4667f, 0.8667f, 1.0f },
    { 0.30f, 0.0000f, 0.6000f, 0.8667f, 1.0f },
    { 0.35f, 0.0000f, 0.6667f, 0.6667f, 1.0f },
    { 0.40f, 0.0000f, 0.6667f, 0.5333f, 1.0f },
    { 0.45f, 0.0000f, 0.6000f, 0.0000f, 1.0f },
    { 0.50f, 0.0000f, 0.7333f, 0.0000f, 1.0f },
    { 0.55f, 0.0000f, 0.8667f, 0.0000f, 1.0f },
    { 0.60f, 0.0000f, 1.0000f, 0.0000f, 1.0f },
    { 0.65f, 0.7333f, 1.0000f, 0.0000f, 1.0f },
    { 0.70f, 0.9333f, 0.9333f, 0.0000f, 1.0f },
    { 0.75f, 1.0000f, 0.8000f, 0.0000f, 1.0f },
    { 0.80f, 1.0000f, 0.6000f, 0.0000f, 1.0f },
    { 0.85f, 1.0000f, 0.0000f, 0.0000f, 1.0f },
    { 0.90f, 0.8667f, 0.0000f, 0.0000f, 1.0f },
    { 0.95f, 0.8000f, 0.0000f, 0.0000f, 1.0f },
    { 1.00f, 0.8000f, 0.8000f, 0.8000f, 1.0f },
};

// --- Red ---
static const ControlPoint kRed[] = {
    { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f },
    { 1.0f, 1.0f, 0.0f, 0.0f, 1.0f },
};

// --- Green ---
static const ControlPoint kGreen[] = {
    { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f },
    { 1.0f, 0.0f, 1.0f, 0.0f, 1.0f },
};

// --- Blue ---
static const ControlPoint kBlue[] = {
    { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f },
    { 1.0f, 0.0f, 0.0f, 1.0f, 1.0f },
};

// --- Negative Red (red → black) ---
static const ControlPoint kNegRed[] = {
    { 0.0f, 1.0f, 0.0f, 0.0f, 1.0f },
    { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f },
};

// --- Negative Green (green → black) ---
static const ControlPoint kNegGreen[] = {
    { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f },
    { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f },
};

// --- Negative Blue (blue → black) ---
static const ControlPoint kNegBlue[] = {
    { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f },
    { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f },
};

// --- Contour (6 banded segments with discontinuities) ---
static const ControlPoint kContour[] = {
    { 0.000f, 0.0f, 0.0f, 0.3f, 1.0f },
    { 0.166f, 0.0f, 0.0f, 1.0f, 1.0f },
    { 0.166f, 0.0f, 0.3f, 0.3f, 1.0f },
    { 0.333f, 0.0f, 1.0f, 1.0f, 1.0f },
    { 0.333f, 0.0f, 0.3f, 0.0f, 1.0f },
    { 0.500f, 0.0f, 1.0f, 0.0f, 1.0f },
    { 0.500f, 0.3f, 0.3f, 0.0f, 1.0f },
    { 0.666f, 1.0f, 1.0f, 0.0f, 1.0f },
    { 0.666f, 0.3f, 0.0f, 0.0f, 1.0f },
    { 0.833f, 1.0f, 0.0f, 0.0f, 1.0f },
    { 0.833f, 0.3f, 0.3f, 0.3f, 1.0f },
    { 1.000f, 1.0f, 1.0f, 1.0f, 1.0f },
};

/// Helper macro: number of elements in a C array.
template<typename T, int N>
constexpr int countOf(const T (&)[N]) { return N; }

/// Build the LUT for a given type.
ColourLut buildLut(ColourMapType type)
{
    switch (type)
    {
    case ColourMapType::GrayScale:
        return buildFromControlPoints(kGrayScale, countOf(kGrayScale));
    case ColourMapType::HotMetal:
        return buildFromControlPoints(kHotMetal, countOf(kHotMetal));
    case ColourMapType::HotMetalNeg:
        return buildFromControlPoints(kHotMetalNeg, countOf(kHotMetalNeg));
    case ColourMapType::ColdMetal:
        return buildFromControlPoints(kColdMetal, countOf(kColdMetal));
    case ColourMapType::ColdMetalNeg:
        return buildFromControlPoints(kColdMetalNeg, countOf(kColdMetalNeg));
    case ColourMapType::GreenMetal:
        return buildFromControlPoints(kGreenMetal, countOf(kGreenMetal));
    case ColourMapType::GreenMetalNeg:
        return buildFromControlPoints(kGreenMetalNeg, countOf(kGreenMetalNeg));
    case ColourMapType::LimeMetal:
        return buildFromControlPoints(kLimeMetal, countOf(kLimeMetal));
    case ColourMapType::LimeMetalNeg:
        return buildFromControlPoints(kLimeMetalNeg, countOf(kLimeMetalNeg));
    case ColourMapType::RedMetal:
        return buildFromControlPoints(kRedMetal, countOf(kRedMetal));
    case ColourMapType::RedMetalNeg:
        return buildFromControlPoints(kRedMetalNeg, countOf(kRedMetalNeg));
    case ColourMapType::PurpleMetal:
        return buildFromControlPoints(kPurpleMetal, countOf(kPurpleMetal));
    case ColourMapType::PurpleMetalNeg:
        return buildFromControlPoints(kPurpleMetalNeg, countOf(kPurpleMetalNeg));
    case ColourMapType::Spectral:
        return buildFromControlPoints(kSpectral, countOf(kSpectral));
    case ColourMapType::Red:
        return buildFromControlPoints(kRed, countOf(kRed));
    case ColourMapType::Green:
        return buildFromControlPoints(kGreen, countOf(kGreen));
    case ColourMapType::Blue:
        return buildFromControlPoints(kBlue, countOf(kBlue));
    case ColourMapType::NegRed:
        return buildFromControlPoints(kNegRed, countOf(kNegRed));
    case ColourMapType::NegGreen:
        return buildFromControlPoints(kNegGreen, countOf(kNegGreen));
    case ColourMapType::NegBlue:
        return buildFromControlPoints(kNegBlue, countOf(kNegBlue));
    case ColourMapType::Contour:
        return buildFromControlPoints(kContour, countOf(kContour));
    default:
        return buildFromControlPoints(kGrayScale, countOf(kGrayScale));
    }
}

} // anonymous namespace

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------

std::string_view colourMapName(ColourMapType type)
{
    switch (type)
    {
    case ColourMapType::GrayScale:      return "Gray";
    case ColourMapType::HotMetal:       return "Hot Metal";
    case ColourMapType::HotMetalNeg:    return "Hot Metal (neg)";
    case ColourMapType::ColdMetal:      return "Cold Metal";
    case ColourMapType::ColdMetalNeg:   return "Cold Metal (neg)";
    case ColourMapType::GreenMetal:     return "Green Metal";
    case ColourMapType::GreenMetalNeg:  return "Green Metal (neg)";
    case ColourMapType::LimeMetal:      return "Lime Metal";
    case ColourMapType::LimeMetalNeg:   return "Lime Metal (neg)";
    case ColourMapType::RedMetal:       return "Red Metal";
    case ColourMapType::RedMetalNeg:    return "Red Metal (neg)";
    case ColourMapType::PurpleMetal:    return "Purple Metal";
    case ColourMapType::PurpleMetalNeg: return "Purple Metal (neg)";
    case ColourMapType::Spectral:       return "Spectral";
    case ColourMapType::Red:            return "Red";
    case ColourMapType::Green:          return "Green";
    case ColourMapType::Blue:           return "Blue";
    case ColourMapType::NegRed:         return "Red (neg)";
    case ColourMapType::NegGreen:       return "Green (neg)";
    case ColourMapType::NegBlue:        return "Blue (neg)";
    case ColourMapType::Contour:        return "Contour";
    default:                            return "Unknown";
    }
}

std::optional<ColourMapType> colourMapByName(std::string_view name)
{
    for (int i = 0; i < static_cast<int>(ColourMapType::Count); ++i)
    {
        auto type = static_cast<ColourMapType>(i);
        if (colourMapName(type) == name)
            return type;
    }
    return std::nullopt;
}

ColourMapRGBA colourMapRepresentative(ColourMapType type)
{
    // Sample the LUT at ~75% to get a visually distinctive colour.
    const ColourLut& lut = colourMapLut(type);
    uint32_t packed = lut.table[192];
    float r = static_cast<float>((packed >>  0) & 0xFF) / 255.0f;
    float g = static_cast<float>((packed >>  8) & 0xFF) / 255.0f;
    float b = static_cast<float>((packed >> 16) & 0xFF) / 255.0f;
    float a = static_cast<float>((packed >> 24) & 0xFF) / 255.0f;
    return { r, g, b, a };
}

const ColourLut& colourMapLut(ColourMapType type)
{
    // Lazy-initialised static array — built once on first access.
    static std::array<ColourLut, colourMapCount()> luts = []()
    {
        std::array<ColourLut, colourMapCount()> arr;
        for (int i = 0; i < colourMapCount(); ++i)
        {
            arr[i] = buildLut(static_cast<ColourMapType>(i));
        }
        return arr;
    }();

    int idx = static_cast<int>(type);
    if (idx < 0 || idx >= colourMapCount())
        idx = 0;
    return luts[idx];
}
