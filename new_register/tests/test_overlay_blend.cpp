/// test_overlay_blend.cpp — regression test for renderOverlaySlice() alpha blending.
///
/// Scenario: anatomical (GrayScale) as vol0 + colored label (Viridis) as vol1.
/// The bug: only the colored label is visible — the grayscale anatomical disappears.
///
/// No external files needed — all volumes are synthesised in memory.
///
/// Tests:
///   A1. renderSlice sanity: anatomical (all-zero, GrayScale) → black
///   A2. renderSlice sanity: label (all-one, GrayScale) → white
///   B.  blend with GrayScale label: both volumes visible (baseline)
///   C.  blend GrayScale anat + VIRIDIS colored label → regression test
///       Viridis[255].B ≈ 37; GrayScale[128].B = 128.
///       Correct blend: B ≈ (128+37)/2 = 82   (anatomical contributes)
///       Regression:    B ≈ 37                 (only colored label visible)
///   D.  anat-only  (label alpha=0) → all black

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "ColourMap.h"
#include "SliceRenderer.h"
#include "Volume.h"

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) \
    std::cerr << "  TEST: " << name << " ... ";

#define PASS() \
    do { std::cerr << "PASS\n"; ++testsPassed; } while (0)

#define FAIL(msg) \
    do { std::cerr << "FAIL: " << msg << "\n"; ++testsFailed; } while (0)

// ---------------------------------------------------------------------------
// Build a 4x4x4 synthetic volume in memory.
//   value      : constant float to fill all voxels
//   isLabel    : call setLabelVolume(true) when true
// voxelToWorld / worldToVoxel stay as identity (default).
// ---------------------------------------------------------------------------
static Volume makeSyntheticVolume(float value, bool isLabel)
{
    Volume v;
    v.dimensions = glm::ivec3(4, 4, 4);
    v.data.assign(4 * 4 * 4, value);
    v.min_value = 0.0f;
    v.max_value = 1.0f;
    if (isLabel)
        v.setLabelVolume(true);
    return v;
}

int main()
{
    std::cerr << "=== OverlayBlendTest ===\n\n";

    const int sliceZ = 0;  // axial slice z=0

    // -----------------------------------------------------------------------
    // Test A1: renderSlice(vol0) — anatomical alone → all black
    // -----------------------------------------------------------------------
    {
        TEST("renderSlice anatomical (all-zero, GrayScale) → all black pixels");

        Volume anat = makeSyntheticVolume(0.0f, false);
        VolumeRenderParams p;
        p.valueMin = 0.0f; p.valueMax = 1.0f;
        p.colourMap = ColourMapType::GrayScale;

        RenderedSlice s = renderSlice(anat, p, 0, sliceZ);
        bool allBlack = (s.width == 4 && s.height == 4 && !s.pixels.empty());
        for (uint32_t px : s.pixels)
            if ((px & 0xFF) != 0) { allBlack = false; break; }

        if (allBlack)
            PASS();
        else
            FAIL("expected all-black; first R=" +
                 std::to_string(s.pixels.empty() ? 0u : (s.pixels[0] & 0xFF)));
    }

    // -----------------------------------------------------------------------
    // Test A2: renderSlice(vol_label_gray) — GrayScale label alone → all white
    //   label ID=1, 1 unique label → idx=(0+1)*255/1=255 → GrayScale[255]=white
    // -----------------------------------------------------------------------
    {
        TEST("renderSlice label (all-one, GrayScale) → all white pixels");

        Volume lbl = makeSyntheticVolume(1.0f, true);
        VolumeRenderParams p;
        p.valueMin = 0.0f; p.valueMax = 1.0f;
        p.colourMap = ColourMapType::GrayScale;

        RenderedSlice s = renderSlice(lbl, p, 0, sliceZ);
        bool allWhite = (s.width == 4 && s.height == 4 && !s.pixels.empty());
        for (uint32_t px : s.pixels)
            if ((px & 0xFF) != 255) { allWhite = false; break; }

        if (allWhite)
            PASS();
        else
            FAIL("expected all-white; first R=" +
                 std::to_string(s.pixels.empty() ? 0u : (s.pixels[0] & 0xFF)));
    }

    // -----------------------------------------------------------------------
    // Test B: GrayScale anatomical (black) + GrayScale label (white) at α=0.5
    //   Correct: R=(0+255)/2=127.5→128  Bug (anat invisible): R=255
    // -----------------------------------------------------------------------
    {
        TEST("blend black-anat + white-GrayScale-label α=0.5/0.5 → mid-gray R≈128");

        Volume anat = makeSyntheticVolume(0.0f, false);
        Volume lbl  = makeSyntheticVolume(1.0f, true);

        VolumeRenderParams p0, p1;
        p0.valueMin = 0.0f; p0.valueMax = 1.0f;
        p0.colourMap = ColourMapType::GrayScale;
        p0.overlayAlpha = 0.5f;
        p1.valueMin = 0.0f; p1.valueMax = 1.0f;
        p1.colourMap = ColourMapType::GrayScale;
        p1.overlayAlpha = 0.5f;

        RenderedSlice s = renderOverlaySlice({&anat, &lbl}, {p0, p1}, 0, sliceZ);

        bool ok = (s.width == 4 && s.height == 4 && !s.pixels.empty());
        uint8_t badR = 0;
        for (uint32_t px : s.pixels)
        {
            uint8_t r = px & 0xFF;
            if (r < 120 || r > 135) { ok = false; badR = r; break; }
        }
        if (ok)
            PASS();
        else
            FAIL("R=" + std::to_string(badR) + " (expected ≈128)");
    }

    // -----------------------------------------------------------------------
    // Test C: GrayScale anatomical (mid-gray) + VIRIDIS colored label
    //
    //   vol0: all voxels = 0.5f → GrayScale[128] = (128, 128, 128)
    //   vol1: label=1 (all voxels = 1.0f), Viridis colormap
    //         idx=(0+1)*255/1=255 → Viridis[255] ≈ (253, 231, 37)
    //
    //   With α=0.5/0.5:
    //     Correct blend: R≈(128+253)/2=190  G≈(128+231)/2=179  B≈(128+37)/2=82
    //     Regression:    R≈253              G≈231               B≈37
    //
    //   The B channel is the decisive discriminator (82 vs 37).
    //   R and G are also checked to confirm the anatomical contributes.
    // -----------------------------------------------------------------------
    {
        TEST("blend mid-gray-anat + VIRIDIS colored label α=0.5/0.5 → both volumes visible");

        Volume anat = makeSyntheticVolume(0.5f, false);   // mid-gray
        Volume lbl  = makeSyntheticVolume(1.0f, true);    // label=1

        VolumeRenderParams p0, p1;
        p0.valueMin = 0.0f; p0.valueMax = 1.0f;
        p0.colourMap = ColourMapType::GrayScale;
        p0.overlayAlpha = 0.5f;
        p1.valueMin = 0.0f; p1.valueMax = 1.0f;
        p1.colourMap = ColourMapType::Viridis;   // colored LUT
        p1.overlayAlpha = 0.5f;

        RenderedSlice s = renderOverlaySlice({&anat, &lbl}, {p0, p1}, 0, sliceZ);

        if (s.width != 4 || s.height != 4 || s.pixels.empty())
        {
            FAIL("wrong dimensions: " + std::to_string(s.width) + "x" +
                 std::to_string(s.height));
        }
        else
        {
            // Sample the first pixel and check all three channels.
            uint8_t r = (s.pixels[0] >>  0) & 0xFF;
            uint8_t g = (s.pixels[0] >>  8) & 0xFF;
            uint8_t b = (s.pixels[0] >> 16) & 0xFF;

            // Viridis[255] ≈ (253,231,37); GrayScale[128] = (128,128,128)
            // Correct blend: R∈[180,200]  G∈[165,190]  B∈[65,100]
            // Regression (only Viridis): R≈253 G≈231 B≈37
            bool r_ok = (r >= 180 && r <= 200);
            bool g_ok = (g >= 165 && g <= 190);
            bool b_ok = (b >=  65 && b <= 100);

            if (r_ok && g_ok && b_ok)
            {
                PASS();
            }
            else
            {
                std::string msg = "pixel RGB=(" +
                    std::to_string(r) + "," + std::to_string(g) + "," +
                    std::to_string(b) + ")";
                if (!b_ok)
                    msg += " — B=" + std::to_string(b) +
                           " (expected 65–100; ≈37 confirms regression:"
                           " only Viridis label visible, anatomical invisible)";
                FAIL(msg);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Test D: anat-only (label alpha=0) → all black
    // -----------------------------------------------------------------------
    {
        TEST("renderOverlaySlice α=0.5/0 (anat-only) → all black");

        Volume anat = makeSyntheticVolume(0.0f, false);
        Volume lbl  = makeSyntheticVolume(1.0f, true);

        VolumeRenderParams p0, p1;
        p0.valueMin = 0.0f; p0.valueMax = 1.0f;
        p0.colourMap = ColourMapType::GrayScale;
        p0.overlayAlpha = 0.5f;
        p1.valueMin = 0.0f; p1.valueMax = 1.0f;
        p1.colourMap = ColourMapType::Viridis;
        p1.overlayAlpha = 0.0f;  // label skipped in pre-loop

        RenderedSlice s = renderOverlaySlice({&anat, &lbl}, {p0, p1}, 0, sliceZ);

        bool allBlack = !s.pixels.empty();
        uint8_t badR = 0;
        for (uint32_t px : s.pixels)
        {
            uint8_t r = px & 0xFF;
            if (r != 0) { allBlack = false; badR = r; break; }
        }
        if (allBlack)
            PASS();
        else
            FAIL("expected R=0 with only anatomical; got R=" + std::to_string(badR));
    }

    // -----------------------------------------------------------------------
    // Summary
    // -----------------------------------------------------------------------
    std::cerr << "\n" << testsPassed << " passed, " << testsFailed << " failed\n";
    return (testsFailed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
