/// test_robust_nonfinite.cpp — regression test for volumes containing
/// non-finite voxel values (+inf, -inf, NaN).
///
/// The MINC file model_mlog_q_sex__Male.mnc contains +inf voxels which
/// previously caused a segfault in renderSlice/renderOverlaySlice via
/// `static_cast<int>(NaN)` indexing into the colour-map LUT.
///
/// Policy:
///   - Volume::load() preserves non-finite voxels in vol.data.
///   - min/max scan ignores non-finite voxels (they don't distort range).
///   - Renderer maps +Inf -> over colour, -Inf and NaN -> under colour.

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "ColourMap.h"
#include "SliceRenderer.h"
#include "Volume.h"

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) std::cerr << "  TEST: " << name << " ... ";
#define PASS() do { std::cerr << "PASS\n"; ++testsPassed; } while (0)
#define FAIL(msg) do { std::cerr << "FAIL: " << msg << "\n"; ++testsFailed; } while (0)

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: test_robust_nonfinite <volume.mnc>\n";
        return 1;
    }
    std::string volumePath = argv[1];

    Volume vol;
    try
    {
        vol.load(volumePath);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to load volume: " << e.what() << "\n";
        return 1;
    }

    std::cerr << "Loaded: " << volumePath << " ("
              << vol.dimensions.x << "x" << vol.dimensions.y << "x" << vol.dimensions.z
              << ", range=[" << vol.min_value << "," << vol.max_value << "])\n\n";

    // --- Test 1: min/max scan ignores non-finite voxels and yields a finite range. ---
    {
        TEST("Volume min_value and max_value are finite");
        if (std::isfinite(vol.min_value) && std::isfinite(vol.max_value)
            && vol.min_value < vol.max_value)
            PASS();
        else
            FAIL("min=" + std::to_string(vol.min_value) +
                 " max=" + std::to_string(vol.max_value));
    }

    // --- Test 2: vol.data preserves non-finite voxels (data is not mutated). ---
    {
        TEST("vol.data preserves non-finite voxels");
        size_t nonFinite = 0;
        for (float v : vol.data)
        {
            if (!std::isfinite(v)) ++nonFinite;
        }
        // The test file is known to contain non-finite voxels; verify they
        // were not silently overwritten during load.
        if (nonFinite > 0)
            PASS();
        else
            FAIL("expected non-finite voxels to remain, found 0");
    }

    // --- Test 3: renderSlice does not crash and produces fully-opaque pixels. ---
    for (int view = 0; view < 3; ++view)
    {
        const char* viewName = (view == 0) ? "axial" : (view == 1) ? "sagittal" : "coronal";
        TEST(std::string("renderSlice ") + viewName + " produces valid opaque pixels");

        VolumeRenderParams params;
        params.valueMin = vol.min_value;
        params.valueMax = vol.max_value;
        params.colourMap = ColourMapType::GrayScale;

        int dim = (view == 0) ? vol.dimensions.z
                : (view == 1) ? vol.dimensions.x
                              : vol.dimensions.y;
        int mid = dim / 2;

        RenderedSlice slice = renderSlice(vol, params, view, mid);

        int expectedW = (view == 0) ? vol.dimensions.x
                      : (view == 1) ? vol.dimensions.y
                                    : vol.dimensions.x;
        int expectedH = (view == 0) ? vol.dimensions.y
                      : (view == 1) ? vol.dimensions.z
                                    : vol.dimensions.z;

        if (slice.width != expectedW || slice.height != expectedH ||
            static_cast<int>(slice.pixels.size()) != expectedW * expectedH)
        {
            FAIL("dims/size mismatch");
            continue;
        }

        bool allOpaque = true;
        for (uint32_t px : slice.pixels)
        {
            if ((px >> 24) != 0xFF)
            {
                allOpaque = false;
                break;
            }
        }
        if (allOpaque)
            PASS();
        else
            FAIL("some pixels not opaque (alpha != 0xFF)");
    }

    // --- Test 4: renderOverlaySlice with same volume twice. ---
    {
        TEST("renderOverlaySlice with two volumes does not crash");

        Volume vol2;
        try
        {
            vol2.load(volumePath);
        }
        catch (const std::exception& e)
        {
            FAIL(std::string("second load failed: ") + e.what());
        }

        std::vector<const Volume*> vols = {&vol, &vol2};
        std::vector<VolumeRenderParams> pars(2);
        pars[0].valueMin = vol.min_value;
        pars[0].valueMax = vol.max_value;
        pars[0].colourMap = ColourMapType::GrayScale;
        pars[0].overlayAlpha = 0.5f;
        pars[1].valueMin = vol2.min_value;
        pars[1].valueMax = vol2.max_value;
        pars[1].colourMap = ColourMapType::HotMetal;
        pars[1].overlayAlpha = 0.5f;

        int midZ = vol.dimensions.z / 2;
        RenderedSlice overlay = renderOverlaySlice(vols, pars, 0, midZ);

        if (overlay.width == vol.dimensions.x &&
            overlay.height == vol.dimensions.y &&
            !overlay.pixels.empty())
            PASS();
        else
            FAIL("overlay dims wrong: " + std::to_string(overlay.width) +
                 "x" + std::to_string(overlay.height));
    }

    // --- Test 5: synthetic volume — pin the exact colour mapping for non-finite values. ---
    // Build a 4x4x4 volume whose first row at z=0 contains:
    //   (0,0,0)=1.0  (finite, mid-range)
    //   (1,0,0)=+Inf (must map to over colour)
    //   (2,0,0)=-Inf (must map to under colour)
    //   (3,0,0)=NaN  (must map to under colour)
    // All other voxels = 5.0.  Range = [0, 10] so 1.0 is mid-grey.
    {
        Volume synth;
        synth.dimensions = glm::ivec3(4, 4, 4);
        synth.data.assign(4 * 4 * 4, 5.0f);
        synth.data[0 * 16 + 0 * 4 + 0] = 1.0f;
        synth.data[0 * 16 + 0 * 4 + 1] = std::numeric_limits<float>::infinity();
        synth.data[0 * 16 + 0 * 4 + 2] = -std::numeric_limits<float>::infinity();
        synth.data[0 * 16 + 0 * 4 + 3] = std::nanf("");

        VolumeRenderParams params;
        params.valueMin = 0.0;
        params.valueMax = 10.0;
        params.colourMap = ColourMapType::GrayScale;

        RenderedSlice slice = renderSlice(synth, params, 0, 0);

        // renderSlice axial: pixels[(h-1-y)*w + x].  For y=0, h=4:
        // dstOff = 3*4 = 12.
        const ColourLut& lut = colourMapLut(ColourMapType::GrayScale);
        uint32_t expectedUnder = lut.table[0];
        uint32_t expectedOver  = lut.table[255];

        TEST("synthetic +Inf voxel renders as over colour");
        if (slice.pixels.size() == 16 && slice.pixels[13] == expectedOver)
            PASS();
        else
            FAIL("got 0x" + [&]() {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%08X",
                    slice.pixels.size() > 13 ? slice.pixels[13] : 0u);
                return std::string(buf);
            }() + " expected 0x" + [&]() {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%08X", expectedOver);
                return std::string(buf);
            }());

        TEST("synthetic -Inf voxel renders as under colour");
        if (slice.pixels.size() == 16 && slice.pixels[14] == expectedUnder)
            PASS();
        else
            FAIL("under-colour mismatch at pixel 14");

        TEST("synthetic NaN voxel renders as under colour");
        if (slice.pixels.size() == 16 && slice.pixels[15] == expectedUnder)
            PASS();
        else
            FAIL("under-colour mismatch at pixel 15");

        TEST("synthetic finite voxel renders within LUT (not under/over)");
        if (slice.pixels.size() == 16 &&
            slice.pixels[12] != expectedUnder &&
            slice.pixels[12] != expectedOver)
            PASS();
        else
            FAIL("finite voxel collapsed to under/over colour");
    }

    // --- Test 6: caller passes non-finite range directly (defense in depth). ---
    {
        TEST("renderSlice tolerates inf valueMax in params");
        VolumeRenderParams params;
        params.valueMin = 0.0;
        params.valueMax = std::numeric_limits<double>::infinity();
        params.colourMap = ColourMapType::GrayScale;

        int midZ = vol.dimensions.z / 2;
        RenderedSlice slice = renderSlice(vol, params, 0, midZ);

        if (static_cast<int>(slice.pixels.size()) ==
            vol.dimensions.x * vol.dimensions.y)
        {
            bool allOpaque = true;
            for (uint32_t px : slice.pixels)
            {
                if ((px >> 24) != 0xFF) { allOpaque = false; break; }
            }
            if (allOpaque) PASS();
            else FAIL("non-opaque pixels with inf range");
        }
        else
        {
            FAIL("wrong pixel count");
        }
    }

    {
        TEST("renderSlice tolerates NaN valueMin in params");
        VolumeRenderParams params;
        params.valueMin = std::nan("");
        params.valueMax = 1.0;
        params.colourMap = ColourMapType::GrayScale;

        int midZ = vol.dimensions.z / 2;
        RenderedSlice slice = renderSlice(vol, params, 0, midZ);

        if (static_cast<int>(slice.pixels.size()) ==
            vol.dimensions.x * vol.dimensions.y)
        {
            bool allOpaque = true;
            for (uint32_t px : slice.pixels)
            {
                if ((px >> 24) != 0xFF) { allOpaque = false; break; }
            }
            if (allOpaque) PASS();
            else FAIL("non-opaque pixels with NaN range");
        }
        else
        {
            FAIL("wrong pixel count");
        }
    }

    std::cerr << "\n" << testsPassed << " passed, " << testsFailed << " failed\n";
    return testsFailed > 0 ? 1 : 0;
}
