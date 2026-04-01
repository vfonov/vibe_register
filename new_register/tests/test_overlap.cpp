/// test_overlap.cpp — overlay rendering correctness test.
///
/// Usage: test_overlap <sq1.mnc> <sq2_tr.mnc> <tests_dir>
///
/// Renders the axial, sagittal, and coronal central slices of a two-volume
/// overlay (sq1 in GrayScale, sq2_tr in GrayScale) using renderOverlaySlice()
/// and pixel-compares the output against reference PNGs in <tests_dir>:
///   correct_overlap_ax.png, correct_overlap_sa.png, correct_overlap_co.png
///
/// sq1.mnc and sq2_tr.mnc have different voxel samplings; the test validates
/// that world-space resampling produces the expected composite image.

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <cmath>
#include <cstdio>
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
// PNG comparison
// ---------------------------------------------------------------------------

/// Load a reference PNG and compare pixel-by-pixel against a rendered slice.
/// Allows ±1 per channel to absorb integer rounding.
/// Returns the number of mismatched pixels (0 = perfect match).
static int comparePng(const std::string& refPath,
                      const std::vector<uint32_t>& rendered,
                      int width, int height)
{
    int refW = 0, refH = 0, channels = 0;
    unsigned char* data = stbi_load(refPath.c_str(), &refW, &refH, &channels, 4);
    if (!data)
    {
        std::cerr << "\n    [error] cannot load reference PNG: " << refPath << "\n";
        return -1;
    }

    int mismatches = 0;

    if (refW != width || refH != height)
    {
        std::cerr << "\n    [error] size mismatch: reference " << refW << "x" << refH
                  << " vs rendered " << width << "x" << height << "\n";
        stbi_image_free(data);
        return -1;
    }

    for (int i = 0; i < width * height; ++i)
    {
        // rendered: 0xAABBGGRR  → R in bits 0-7, G in 8-15, B in 16-23
        uint32_t p = rendered[i];
        int rR = (p >>  0) & 0xFF;
        int rG = (p >>  8) & 0xFF;
        int rB = (p >> 16) & 0xFF;

        // stb_image: interleaved R,G,B,A bytes
        int pR = data[i * 4 + 0];
        int pG = data[i * 4 + 1];
        int pB = data[i * 4 + 2];

        if (std::abs(rR - pR) > 1 || std::abs(rG - pG) > 1 || std::abs(rB - pB) > 1)
            ++mismatches;
    }

    stbi_image_free(data);
    return mismatches;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::cerr << "Usage: test_overlap <sq1.mnc> <sq2_tr.mnc> <tests_dir>\n";
        return 1;
    }

    std::string vol1Path  = argv[1];
    std::string vol2Path  = argv[2];
    std::string testsDir  = argv[3];

    // Ensure trailing slash
    if (!testsDir.empty() && testsDir.back() != '/')
        testsDir += '/';

    // --- Load volumes ---
    Volume vol1, vol2;
    try { vol1.load(vol1Path); }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to load " << vol1Path << ": " << e.what() << "\n";
        return 1;
    }
    try { vol2.load(vol2Path); }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to load " << vol2Path << ": " << e.what() << "\n";
        return 1;
    }

    std::cerr << "vol1: " << vol1Path
              << " (" << vol1.dimensions.x << "x" << vol1.dimensions.y << "x" << vol1.dimensions.z
              << ", range=[" << vol1.min_value << "," << vol1.max_value << "])\n";
    std::cerr << "vol2: " << vol2Path
              << " (" << vol2.dimensions.x << "x" << vol2.dimensions.y << "x" << vol2.dimensions.z
              << ", range=[" << vol2.min_value << "," << vol2.max_value << "])\n\n";

    // --- Render parameters ---
    std::vector<const Volume*> vols = { &vol1, &vol2 };

    std::vector<VolumeRenderParams> pars(2);
    pars[0].valueMin    = vol1.min_value;
    pars[0].valueMax    = vol1.max_value;
    pars[0].colourMap   = ColourMapType::GrayScale;
    pars[0].overlayAlpha = 0.5f;

    pars[1].valueMin    = vol2.min_value;
    pars[1].valueMax    = vol2.max_value;
    pars[1].colourMap   = ColourMapType::GrayScale;
    pars[1].overlayAlpha = 0.5f;

    // Central slice indices in the reference volume (vol1)
    int centerZ = vol1.dimensions.z / 2;
    int centerX = vol1.dimensions.x / 2;
    int centerY = vol1.dimensions.y / 2;

    // --- Test 1: axial (view 0) ---
    {
        TEST("OverlapAxial");
        RenderedSlice slice = renderOverlaySlice(vols, pars, 0, centerZ, nullptr);
        if (slice.pixels.empty())
        {
            FAIL("rendered slice is empty");
        }
        else
        {
            std::string ref = testsDir + "correct_overlap_ax.png";
            int mm = comparePng(ref, slice.pixels, slice.width, slice.height);
            if (mm < 0)
                FAIL("could not load reference PNG");
            else if (mm > 0)
                FAIL(std::to_string(mm) + " pixel(s) differ from reference");
            else
                PASS();
        }
    }

    // --- Test 2: sagittal (view 1) ---
    {
        TEST("OverlapSagittal");
        RenderedSlice slice = renderOverlaySlice(vols, pars, 1, centerX, nullptr);
        if (slice.pixels.empty())
        {
            FAIL("rendered slice is empty");
        }
        else
        {
            std::string ref = testsDir + "correct_overlap_sa.png";
            int mm = comparePng(ref, slice.pixels, slice.width, slice.height);
            if (mm < 0)
                FAIL("could not load reference PNG");
            else if (mm > 0)
                FAIL(std::to_string(mm) + " pixel(s) differ from reference");
            else
                PASS();
        }
    }

    // --- Test 3: coronal (view 2) ---
    {
        TEST("OverlapCoronal");
        RenderedSlice slice = renderOverlaySlice(vols, pars, 2, centerY, nullptr);
        if (slice.pixels.empty())
        {
            FAIL("rendered slice is empty");
        }
        else
        {
            std::string ref = testsDir + "correct_overlap_co.png";
            int mm = comparePng(ref, slice.pixels, slice.width, slice.height);
            if (mm < 0)
                FAIL("could not load reference PNG");
            else if (mm > 0)
                FAIL(std::to_string(mm) + " pixel(s) differ from reference");
            else
                PASS();
        }
    }

    std::cerr << "\n" << testsPassed << " passed, " << testsFailed << " failed.\n";
    return testsFailed > 0 ? 1 : 0;
}
