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

// --- Viridis (perceptually uniform, matplotlib reference) ---
static const ControlPoint kViridis[] = {
    { 0.00f, 0.267f, 0.004f, 0.329f, 1.0f },
    { 0.05f, 0.283f, 0.141f, 0.458f, 1.0f },
    { 0.10f, 0.283f, 0.232f, 0.536f, 1.0f },
    { 0.15f, 0.271f, 0.309f, 0.584f, 1.0f },
    { 0.20f, 0.248f, 0.377f, 0.614f, 1.0f },
    { 0.25f, 0.220f, 0.439f, 0.632f, 1.0f },
    { 0.30f, 0.191f, 0.496f, 0.640f, 1.0f },
    { 0.35f, 0.164f, 0.550f, 0.641f, 1.0f },
    { 0.40f, 0.139f, 0.603f, 0.632f, 1.0f },
    { 0.45f, 0.122f, 0.653f, 0.612f, 1.0f },
    { 0.50f, 0.127f, 0.702f, 0.578f, 1.0f },
    { 0.55f, 0.166f, 0.749f, 0.531f, 1.0f },
    { 0.60f, 0.244f, 0.791f, 0.469f, 1.0f },
    { 0.65f, 0.348f, 0.827f, 0.396f, 1.0f },
    { 0.70f, 0.472f, 0.855f, 0.310f, 1.0f },
    { 0.75f, 0.601f, 0.875f, 0.222f, 1.0f },
    { 0.80f, 0.733f, 0.884f, 0.141f, 1.0f },
    { 0.85f, 0.854f, 0.887f, 0.098f, 1.0f },
    { 0.90f, 0.945f, 0.890f, 0.153f, 1.0f },
    { 0.95f, 0.985f, 0.905f, 0.306f, 1.0f },
    { 1.00f, 0.993f, 0.906f, 0.144f, 1.0f },
};

// --- Jet (MATLAB classic rainbow) ---
static const ControlPoint kJet[] = {
    { 0.000f, 0.000f, 0.000f, 0.500f, 1.0f },
    { 0.050f, 0.000f, 0.000f, 0.703f, 1.0f },
    { 0.100f, 0.000f, 0.000f, 0.906f, 1.0f },
    { 0.150f, 0.000f, 0.109f, 1.000f, 1.0f },
    { 0.200f, 0.000f, 0.313f, 1.000f, 1.0f },
    { 0.250f, 0.000f, 0.516f, 1.000f, 1.0f },
    { 0.300f, 0.000f, 0.719f, 1.000f, 1.0f },
    { 0.350f, 0.000f, 0.922f, 1.000f, 1.0f },
    { 0.400f, 0.125f, 1.000f, 0.875f, 1.0f },
    { 0.450f, 0.328f, 1.000f, 0.672f, 1.0f },
    { 0.500f, 0.531f, 1.000f, 0.469f, 1.0f },
    { 0.550f, 0.734f, 1.000f, 0.266f, 1.0f },
    { 0.600f, 0.938f, 1.000f, 0.063f, 1.0f },
    { 0.650f, 1.000f, 0.875f, 0.000f, 1.0f },
    { 0.700f, 1.000f, 0.672f, 0.000f, 1.0f },
    { 0.750f, 1.000f, 0.469f, 0.000f, 1.0f },
    { 0.800f, 1.000f, 0.266f, 0.000f, 1.0f },
    { 0.850f, 1.000f, 0.063f, 0.000f, 1.0f },
    { 0.900f, 0.906f, 0.000f, 0.000f, 1.0f },
    { 0.950f, 0.703f, 0.000f, 0.000f, 1.0f },
    { 1.000f, 0.500f, 0.000f, 0.000f, 1.0f },
};

// --- Magma (matplotlib, perceptually uniform sequential) ---
static const ControlPoint kMagma[] = {
    { 0.00f, 0.001f, 0.000f, 0.014f, 1.0f },
    { 0.05f, 0.035f, 0.028f, 0.141f, 1.0f },
    { 0.10f, 0.090f, 0.045f, 0.272f, 1.0f },
    { 0.15f, 0.157f, 0.040f, 0.383f, 1.0f },
    { 0.20f, 0.232f, 0.039f, 0.443f, 1.0f },
    { 0.25f, 0.311f, 0.064f, 0.467f, 1.0f },
    { 0.30f, 0.390f, 0.083f, 0.472f, 1.0f },
    { 0.35f, 0.470f, 0.097f, 0.462f, 1.0f },
    { 0.40f, 0.550f, 0.113f, 0.438f, 1.0f },
    { 0.45f, 0.631f, 0.137f, 0.402f, 1.0f },
    { 0.50f, 0.710f, 0.170f, 0.357f, 1.0f },
    { 0.55f, 0.784f, 0.215f, 0.310f, 1.0f },
    { 0.60f, 0.849f, 0.275f, 0.265f, 1.0f },
    { 0.65f, 0.905f, 0.348f, 0.225f, 1.0f },
    { 0.70f, 0.948f, 0.432f, 0.196f, 1.0f },
    { 0.75f, 0.977f, 0.524f, 0.181f, 1.0f },
    { 0.80f, 0.993f, 0.623f, 0.195f, 1.0f },
    { 0.85f, 0.998f, 0.726f, 0.247f, 1.0f },
    { 0.90f, 0.996f, 0.831f, 0.341f, 1.0f },
    { 0.95f, 0.991f, 0.928f, 0.490f, 1.0f },
    { 1.00f, 0.987f, 0.991f, 0.750f, 1.0f },
};

// --- Inferno (matplotlib, perceptually uniform sequential) ---
static const ControlPoint kInferno[] = {
    { 0.00f, 0.001f, 0.000f, 0.014f, 1.0f },
    { 0.05f, 0.035f, 0.021f, 0.134f, 1.0f },
    { 0.10f, 0.097f, 0.031f, 0.272f, 1.0f },
    { 0.15f, 0.172f, 0.018f, 0.378f, 1.0f },
    { 0.20f, 0.253f, 0.014f, 0.415f, 1.0f },
    { 0.25f, 0.333f, 0.044f, 0.405f, 1.0f },
    { 0.30f, 0.413f, 0.070f, 0.372f, 1.0f },
    { 0.35f, 0.492f, 0.095f, 0.328f, 1.0f },
    { 0.40f, 0.572f, 0.118f, 0.280f, 1.0f },
    { 0.45f, 0.648f, 0.145f, 0.227f, 1.0f },
    { 0.50f, 0.722f, 0.179f, 0.172f, 1.0f },
    { 0.55f, 0.791f, 0.225f, 0.119f, 1.0f },
    { 0.60f, 0.852f, 0.284f, 0.072f, 1.0f },
    { 0.65f, 0.903f, 0.358f, 0.035f, 1.0f },
    { 0.70f, 0.941f, 0.444f, 0.017f, 1.0f },
    { 0.75f, 0.967f, 0.537f, 0.044f, 1.0f },
    { 0.80f, 0.980f, 0.637f, 0.115f, 1.0f },
    { 0.85f, 0.982f, 0.741f, 0.212f, 1.0f },
    { 0.90f, 0.974f, 0.846f, 0.336f, 1.0f },
    { 0.95f, 0.964f, 0.940f, 0.497f, 1.0f },
    { 1.00f, 0.988f, 1.000f, 0.644f, 1.0f },
};

// --- Plasma (matplotlib, perceptually uniform sequential) ---
static const ControlPoint kPlasma[] = {
    { 0.00f, 0.050f, 0.030f, 0.528f, 1.0f },
    { 0.05f, 0.133f, 0.022f, 0.563f, 1.0f },
    { 0.10f, 0.210f, 0.013f, 0.579f, 1.0f },
    { 0.15f, 0.285f, 0.001f, 0.581f, 1.0f },
    { 0.20f, 0.355f, 0.004f, 0.568f, 1.0f },
    { 0.25f, 0.421f, 0.023f, 0.543f, 1.0f },
    { 0.30f, 0.483f, 0.048f, 0.510f, 1.0f },
    { 0.35f, 0.542f, 0.073f, 0.472f, 1.0f },
    { 0.40f, 0.598f, 0.098f, 0.432f, 1.0f },
    { 0.45f, 0.651f, 0.123f, 0.391f, 1.0f },
    { 0.50f, 0.700f, 0.150f, 0.351f, 1.0f },
    { 0.55f, 0.747f, 0.180f, 0.311f, 1.0f },
    { 0.60f, 0.792f, 0.213f, 0.270f, 1.0f },
    { 0.65f, 0.834f, 0.253f, 0.227f, 1.0f },
    { 0.70f, 0.874f, 0.300f, 0.183f, 1.0f },
    { 0.75f, 0.909f, 0.359f, 0.137f, 1.0f },
    { 0.80f, 0.940f, 0.432f, 0.091f, 1.0f },
    { 0.85f, 0.963f, 0.517f, 0.050f, 1.0f },
    { 0.90f, 0.976f, 0.613f, 0.032f, 1.0f },
    { 0.95f, 0.977f, 0.717f, 0.069f, 1.0f },
    { 1.00f, 0.940f, 0.975f, 0.131f, 1.0f },
};

// --- Turbo (Google AI, improved rainbow) ---
static const ControlPoint kTurbo[] = {
    { 0.00f, 0.190f, 0.072f, 0.232f, 1.0f },
    { 0.05f, 0.260f, 0.150f, 0.540f, 1.0f },
    { 0.10f, 0.305f, 0.250f, 0.760f, 1.0f },
    { 0.15f, 0.318f, 0.370f, 0.920f, 1.0f },
    { 0.20f, 0.290f, 0.495f, 0.990f, 1.0f },
    { 0.25f, 0.227f, 0.615f, 0.980f, 1.0f },
    { 0.30f, 0.141f, 0.722f, 0.898f, 1.0f },
    { 0.35f, 0.059f, 0.809f, 0.767f, 1.0f },
    { 0.40f, 0.040f, 0.872f, 0.613f, 1.0f },
    { 0.45f, 0.137f, 0.916f, 0.449f, 1.0f },
    { 0.50f, 0.315f, 0.943f, 0.290f, 1.0f },
    { 0.55f, 0.510f, 0.954f, 0.155f, 1.0f },
    { 0.60f, 0.685f, 0.942f, 0.063f, 1.0f },
    { 0.65f, 0.828f, 0.897f, 0.020f, 1.0f },
    { 0.70f, 0.930f, 0.822f, 0.011f, 1.0f },
    { 0.75f, 0.985f, 0.722f, 0.007f, 1.0f },
    { 0.80f, 0.994f, 0.600f, 0.005f, 1.0f },
    { 0.85f, 0.960f, 0.472f, 0.004f, 1.0f },
    { 0.90f, 0.886f, 0.345f, 0.004f, 1.0f },
    { 0.95f, 0.776f, 0.222f, 0.012f, 1.0f },
    { 1.00f, 0.647f, 0.108f, 0.024f, 1.0f },
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
    case ColourMapType::Viridis:
        return buildFromControlPoints(kViridis, countOf(kViridis));
    case ColourMapType::Jet:
        return buildFromControlPoints(kJet, countOf(kJet));
    case ColourMapType::Magma:
        return buildFromControlPoints(kMagma, countOf(kMagma));
    case ColourMapType::Inferno:
        return buildFromControlPoints(kInferno, countOf(kInferno));
    case ColourMapType::Plasma:
        return buildFromControlPoints(kPlasma, countOf(kPlasma));
    case ColourMapType::Turbo:
        return buildFromControlPoints(kTurbo, countOf(kTurbo));
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
    case ColourMapType::Viridis:        return "Viridis";
    case ColourMapType::Jet:            return "Jet";
    case ColourMapType::Magma:          return "Magma";
    case ColourMapType::Inferno:        return "Inferno";
    case ColourMapType::Plasma:         return "Plasma";
    case ColourMapType::Turbo:          return "Turbo";
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
