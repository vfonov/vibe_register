/// test_mincpik.cpp — rendering correctness tests for SliceRenderer.
///
/// Usage: test_mincpik <path_to_minc_volume>
///
/// Tests:
///   1. renderSlice produces correct dimensions
///   2. renderSlice with different colour maps produces different pixels
///   3. renderSlice value range clamping
///   4. renderOverlaySlice produces correct dimensions with 2 volumes
///   5. Mosaic-style multi-slice rendering (multiple views)

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "ColourMap.h"
#include "SliceRenderer.h"
#include "Volume.h"

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) \
    std::cerr << "  TEST: " << name << " ... "; \

#define PASS() \
    do { std::cerr << "PASS\n"; ++testsPassed; } while (0)

#define FAIL(msg) \
    do { std::cerr << "FAIL: " << msg << "\n"; ++testsFailed; } while (0)

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: test_mincpik <volume.mnc>\n";
        return 1;
    }

    std::string volumePath = argv[1];

    // Load volume
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

    // --- Test 1: renderSlice produces correct dimensions ---
    {
        TEST("renderSlice axial dimensions");
        VolumeRenderParams params;
        params.valueMin = vol.min_value;
        params.valueMax = vol.max_value;
        params.colourMap = ColourMapType::GrayScale;

        int midZ = vol.dimensions.z / 2;
        RenderedSlice slice = renderSlice(vol, params, 0, midZ);

        if (slice.width == vol.dimensions.x && slice.height == vol.dimensions.y &&
            static_cast<int>(slice.pixels.size()) == slice.width * slice.height)
            PASS();
        else
            FAIL("expected " + std::to_string(vol.dimensions.x) + "x" +
                 std::to_string(vol.dimensions.y) + ", got " +
                 std::to_string(slice.width) + "x" + std::to_string(slice.height));
    }

    {
        TEST("renderSlice sagittal dimensions");
        VolumeRenderParams params;
        params.valueMin = vol.min_value;
        params.valueMax = vol.max_value;

        int midX = vol.dimensions.x / 2;
        RenderedSlice slice = renderSlice(vol, params, 1, midX);

        if (slice.width == vol.dimensions.y && slice.height == vol.dimensions.z)
            PASS();
        else
            FAIL("expected " + std::to_string(vol.dimensions.y) + "x" +
                 std::to_string(vol.dimensions.z) + ", got " +
                 std::to_string(slice.width) + "x" + std::to_string(slice.height));
    }

    {
        TEST("renderSlice coronal dimensions");
        VolumeRenderParams params;
        params.valueMin = vol.min_value;
        params.valueMax = vol.max_value;

        int midY = vol.dimensions.y / 2;
        RenderedSlice slice = renderSlice(vol, params, 2, midY);

        if (slice.width == vol.dimensions.x && slice.height == vol.dimensions.z)
            PASS();
        else
            FAIL("expected " + std::to_string(vol.dimensions.x) + "x" +
                 std::to_string(vol.dimensions.z) + ", got " +
                 std::to_string(slice.width) + "x" + std::to_string(slice.height));
    }

    // --- Test 2: Different colour maps produce different pixels ---
    {
        TEST("different colour maps produce different pixels");
        VolumeRenderParams paramsGray;
        paramsGray.valueMin = vol.min_value;
        paramsGray.valueMax = vol.max_value;
        paramsGray.colourMap = ColourMapType::GrayScale;

        VolumeRenderParams paramsHot;
        paramsHot.valueMin = vol.min_value;
        paramsHot.valueMax = vol.max_value;
        paramsHot.colourMap = ColourMapType::HotMetal;

        int midZ = vol.dimensions.z / 2;
        RenderedSlice sliceGray = renderSlice(vol, paramsGray, 0, midZ);
        RenderedSlice sliceHot = renderSlice(vol, paramsHot, 0, midZ);

        bool anyDiff = false;
        for (size_t i = 0; i < sliceGray.pixels.size() && i < sliceHot.pixels.size(); ++i)
        {
            if (sliceGray.pixels[i] != sliceHot.pixels[i])
            {
                anyDiff = true;
                break;
            }
        }
        if (anyDiff)
            PASS();
        else
            FAIL("grayscale and hot metal produced identical pixels");
    }

    // --- Test 3: Value range clamping ---
    {
        TEST("value range clamping");
        // Render with very narrow range — should clamp most values
        VolumeRenderParams paramsFull;
        paramsFull.valueMin = vol.min_value;
        paramsFull.valueMax = vol.max_value;
        paramsFull.colourMap = ColourMapType::GrayScale;

        VolumeRenderParams paramsNarrow;
        double mid = (vol.min_value + vol.max_value) / 2.0;
        double quarter = (vol.max_value - vol.min_value) / 4.0;
        paramsNarrow.valueMin = mid - quarter * 0.01;
        paramsNarrow.valueMax = mid + quarter * 0.01;
        paramsNarrow.colourMap = ColourMapType::GrayScale;

        int midZ = vol.dimensions.z / 2;
        RenderedSlice sliceFull = renderSlice(vol, paramsFull, 0, midZ);
        RenderedSlice sliceNarrow = renderSlice(vol, paramsNarrow, 0, midZ);

        bool anyDiff = false;
        for (size_t i = 0; i < sliceFull.pixels.size() && i < sliceNarrow.pixels.size(); ++i)
        {
            if (sliceFull.pixels[i] != sliceNarrow.pixels[i])
            {
                anyDiff = true;
                break;
            }
        }
        if (anyDiff)
            PASS();
        else
            FAIL("full range and narrow range produced identical pixels");
    }

    // --- Test 4: Empty volume returns empty slice ---
    {
        TEST("empty volume returns empty slice");
        Volume emptyVol;
        VolumeRenderParams params;
        RenderedSlice slice = renderSlice(emptyVol, params, 0, 0);
        if (slice.width == 0 && slice.height == 0 && slice.pixels.empty())
            PASS();
        else
            FAIL("expected empty slice for empty volume");
    }

    // --- Test 5: renderOverlaySlice with two copies of same volume ---
    {
        TEST("renderOverlaySlice with two volumes");
        Volume vol2;
        vol2.load(volumePath);

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

        if (overlay.width == vol.dimensions.x && overlay.height == vol.dimensions.y &&
            !overlay.pixels.empty())
            PASS();
        else
            FAIL("overlay dimensions/pixels wrong: " +
                 std::to_string(overlay.width) + "x" + std::to_string(overlay.height));
    }

    // --- Test 6: renderOverlaySlice returns empty with < 2 volumes ---
    {
        TEST("renderOverlaySlice with single volume returns empty");
        std::vector<const Volume*> vols = {&vol};
        std::vector<VolumeRenderParams> pars(1);
        pars[0].valueMin = vol.min_value;
        pars[0].valueMax = vol.max_value;

        RenderedSlice overlay = renderOverlaySlice(vols, pars, 0, 0);
        if (overlay.width == 0 && overlay.height == 0 && overlay.pixels.empty())
            PASS();
        else
            FAIL("expected empty overlay for single volume");
    }

    // --- Test 7: All pixels are opaque (alpha = 0xFF) ---
    {
        TEST("all pixels have opaque alpha");
        VolumeRenderParams params;
        params.valueMin = vol.min_value;
        params.valueMax = vol.max_value;
        params.colourMap = ColourMapType::GrayScale;

        int midZ = vol.dimensions.z / 2;
        RenderedSlice slice = renderSlice(vol, params, 0, midZ);

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
            FAIL("some pixels have non-opaque alpha");
    }

    // --- Test 8: Row-splitting logic (--rows N) ---
    // Render 6 coronal slices, split into 1 row vs 2 rows, verify counts.
    {
        TEST("row splitting with nRows=1 vs nRows=2");

        // Render 6 coronal slices
        int nSlices = 6;
        int dimCoronal = vol.dimensions.y; // coronal = view 2
        std::vector<RenderedSlice> allSlices;
        for (int i = 0; i < nSlices; ++i)
        {
            int sliceIdx = dimCoronal * (i + 1) / (nSlices + 1);
            VolumeRenderParams params;
            params.valueMin = vol.min_value;
            params.valueMax = vol.max_value;
            params.colourMap = ColourMapType::GrayScale;
            allSlices.push_back(renderSlice(vol, params, 2, sliceIdx));
        }

        // nRows=1: all 6 in a single row
        {
            int nRows = 1;
            int total = static_cast<int>(allSlices.size());
            int perRow = (total + nRows - 1) / nRows;
            int rowCount = 0;
            int maxColsInRow = 0;
            for (int r = 0; r < nRows; ++r)
            {
                int start = r * perRow;
                if (start >= total) break;
                int end = std::min(start + perRow, total);
                ++rowCount;
                maxColsInRow = std::max(maxColsInRow, end - start);
            }
            if (rowCount == 1 && maxColsInRow == 6)
                PASS();
            else
                FAIL("nRows=1 expected 1 row with 6 cols, got " +
                     std::to_string(rowCount) + " rows, max " +
                     std::to_string(maxColsInRow) + " cols");
        }

        // nRows=2: 3 slices per sub-row -> 2 rows
        {
            TEST("row splitting nRows=2 yields 2 rows of 3");
            int nRows = 2;
            int total = static_cast<int>(allSlices.size());
            int perRow = (total + nRows - 1) / nRows;
            int rowCount = 0;
            std::vector<int> colCounts;
            for (int r = 0; r < nRows; ++r)
            {
                int start = r * perRow;
                if (start >= total) break;
                int end = std::min(start + perRow, total);
                ++rowCount;
                colCounts.push_back(end - start);
            }
            if (rowCount == 2 && colCounts[0] == 3 && colCounts[1] == 3)
                PASS();
            else
            {
                std::string msg = "nRows=2 expected 2 rows of 3, got " +
                    std::to_string(rowCount) + " rows [";
                for (size_t i = 0; i < colCounts.size(); ++i)
                {
                    if (i > 0) msg += ",";
                    msg += std::to_string(colCounts[i]);
                }
                msg += "]";
                FAIL(msg);
            }
        }

        // nRows=3: 2 slices per sub-row -> 3 rows
        {
            TEST("row splitting nRows=3 yields 3 rows of 2");
            int nRows = 3;
            int total = static_cast<int>(allSlices.size());
            int perRow = (total + nRows - 1) / nRows;
            int rowCount = 0;
            std::vector<int> colCounts;
            for (int r = 0; r < nRows; ++r)
            {
                int start = r * perRow;
                if (start >= total) break;
                int end = std::min(start + perRow, total);
                ++rowCount;
                colCounts.push_back(end - start);
            }
            if (rowCount == 3 && colCounts[0] == 2 && colCounts[1] == 2 && colCounts[2] == 2)
                PASS();
            else
            {
                std::string msg = "nRows=3 expected 3 rows of 2, got " +
                    std::to_string(rowCount) + " rows [";
                for (size_t i = 0; i < colCounts.size(); ++i)
                {
                    if (i > 0) msg += ",";
                    msg += std::to_string(colCounts[i]);
                }
                msg += "]";
                FAIL(msg);
            }
        }

        // nRows=4: ceiling(6/4)=2 per row -> 3 rows (last has remainder)
        {
            TEST("row splitting nRows=4 yields 3 rows (2,2,2)");
            int nRows = 4;
            int total = static_cast<int>(allSlices.size());
            int perRow = (total + nRows - 1) / nRows;  // ceil(6/4)=2
            int rowCount = 0;
            std::vector<int> colCounts;
            for (int r = 0; r < nRows; ++r)
            {
                int start = r * perRow;
                if (start >= total) break;
                int end = std::min(start + perRow, total);
                ++rowCount;
                colCounts.push_back(end - start);
            }
            // perRow = ceil(6/4) = 2, so rows: [0..2), [2..4), [4..6) => 3 rows of 2
            if (rowCount == 3 && colCounts[0] == 2 && colCounts[1] == 2 && colCounts[2] == 2)
                PASS();
            else
            {
                std::string msg = "nRows=4 expected 3 rows of 2, got " +
                    std::to_string(rowCount) + " rows [";
                for (size_t i = 0; i < colCounts.size(); ++i)
                {
                    if (i > 0) msg += ",";
                    msg += std::to_string(colCounts[i]);
                }
                msg += "]";
                FAIL(msg);
            }
        }
    }

    // --- Summary ---
    std::cerr << "\n" << testsPassed << " passed, " << testsFailed << " failed\n";
    return testsFailed > 0 ? 1 : 0;
}
