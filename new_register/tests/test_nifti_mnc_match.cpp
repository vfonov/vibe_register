/// test_nifti_mnc_match.cpp — verifies NIfTI and MINC files produce identical slices.
///
/// Usage: test_nifti_mnc_match <file.nii.gz> <file.mnc>
///
/// Loads both a NIfTI and MINC file, extracts central axial/sagittal/coronal slices
/// from each, and compares pixel data. The files should produce identical world-space
/// coordinates and slice images.

#include <cmath>
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
// Pixel comparison
// ---------------------------------------------------------------------------

/// Compare two rendered slices pixel-by-pixel.
/// Returns the number of mismatched pixels (0 = perfect match).
static int compareSlices(const RenderedSlice& slice1,
                         const RenderedSlice& slice2)
{
    int mismatches = 0;

    if (slice1.width != slice2.width || slice1.height != slice2.height)
    {
        std::cerr << "\n    [error] size mismatch: " 
                  << slice1.width << "x" << slice1.height
                  << " vs " << slice2.width << "x" << slice2.height << "\n";
        return -1;
    }

    if (slice1.pixels.size() != slice2.pixels.size())
    {
        std::cerr << "\n    [error] pixel count mismatch: " 
                  << slice1.pixels.size() << " vs " << slice2.pixels.size() << "\n";
        return -1;
    }

    for (size_t i = 0; i < slice1.pixels.size(); ++i)
    {
        uint32_t p1 = slice1.pixels[i];
        uint32_t p2 = slice2.pixels[i];

        int rR1 = (p1 >>  0) & 0xFF;
        int rG1 = (p1 >>  8) & 0xFF;
        int rB1 = (p1 >> 16) & 0xFF;

        int rR2 = (p2 >>  0) & 0xFF;
        int rG2 = (p2 >>  8) & 0xFF;
        int rB2 = (p2 >> 16) & 0xFF;

        if (std::abs(rR1 - rR2) > 1 || std::abs(rG1 - rG2) > 1 || std::abs(rB1 - rB2) > 1)
            ++mismatches;
    }

    return mismatches;
}

// ---------------------------------------------------------------------------
// Coordinate comparison
// ---------------------------------------------------------------------------

static void compareCoordinates(const Volume& vol1, const Volume& vol2, const std::string& label)
{
    std::cerr << "\n  Coordinate comparison for " << label << ":\n";
    
    glm::dvec3 corner1, corner2;
    vol1.transformVoxelToWorld(glm::ivec3(0, 0, 0), corner1);
    vol2.transformVoxelToWorld(glm::ivec3(0, 0, 0), corner2);
    
    std::cerr << "    vol1 corner (0,0,0): (" << corner1.x << ", " << corner1.y << ", " << corner1.z << ")\n";
    std::cerr << "    vol2 corner (0,0,0): (" << corner2.x << ", " << corner2.y << ", " << corner2.z << ")\n";
    
    double cornerDist = glm::length(corner1 - corner2);
    std::cerr << "    corner distance: " << cornerDist << " mm\n";
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "Usage: test_nifti_mnc_match <nifti_file> <minc_file>\n";
        return 1;
    }

    std::string niftiPath = argv[1];
    std::string mincPath  = argv[2];

    // --- Load volumes ---
    Volume volNifti, volMinc;
    try { volNifti.load(niftiPath); }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to load " << niftiPath << ": " << e.what() << "\n";
        return 1;
    }
    try { volMinc.load(mincPath); }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to load " << mincPath << ": " << e.what() << "\n";
        return 1;
    }

    std::cerr << "NIfTI: " << niftiPath
              << " (" << volNifti.dimensions.x << "x" << volNifti.dimensions.y << "x" << volNifti.dimensions.z
              << ", range=[" << volNifti.min_value << "," << volNifti.max_value << "])\n";
    std::cerr << "MINC:  " << mincPath
              << " (" << volMinc.dimensions.x << "x" << volMinc.dimensions.y << "x" << volMinc.dimensions.z
              << ", range=[" << volMinc.min_value << "," << volMinc.max_value << "])\n";

    // --- Render parameters (use MINC volume's range for both) ---
    VolumeRenderParams params;
    params.valueMin   = volMinc.min_value;
    params.valueMax   = volMinc.max_value;
    params.colourMap  = ColourMapType::GrayScale;

    // Central slice indices
    int centerZ = volMinc.dimensions.z / 2;
    int centerX = volMinc.dimensions.x / 2;
    int centerY = volMinc.dimensions.y / 2;

    std::cerr << "\n  Central slices: axial=" << centerZ << ", sagittal=" << centerX << ", coronal=" << centerY << "\n";

    // --- Compare coordinates ---
    compareCoordinates(volNifti, volMinc, "corner (0,0,0)");

    // --- Test 1: axial (view 0) ---
    {
        TEST("AxialSlice");
        RenderedSlice sliceNifti = renderSlice(volNifti, params, 0, centerZ);
        RenderedSlice sliceMinc  = renderSlice(volMinc, params, 0, centerZ);
        
        if (sliceNifti.pixels.empty() || sliceMinc.pixels.empty())
        {
            FAIL("rendered slice is empty");
        }
        else
        {
            int mm = compareSlices(sliceNifti, sliceMinc);
            if (mm < 0)
                FAIL("could not compare slices (size mismatch)");
            else if (mm > 0)
                FAIL(std::to_string(mm) + " pixel(s) differ from reference");
            else
                PASS();
        }
    }

    // --- Test 2: sagittal (view 1) ---
    {
        TEST("SagittalSlice");
        RenderedSlice sliceNifti = renderSlice(volNifti, params, 1, centerX);
        RenderedSlice sliceMinc  = renderSlice(volMinc, params, 1, centerX);
        
        if (sliceNifti.pixels.empty() || sliceMinc.pixels.empty())
        {
            FAIL("rendered slice is empty");
        }
        else
        {
            int mm = compareSlices(sliceNifti, sliceMinc);
            if (mm < 0)
                FAIL("could not compare slices (size mismatch)");
            else if (mm > 0)
                FAIL(std::to_string(mm) + " pixel(s) differ from reference");
            else
                PASS();
        }
    }

    // --- Test 3: coronal (view 2) ---
    {
        TEST("CoronalSlice");
        RenderedSlice sliceNifti = renderSlice(volNifti, params, 2, centerY);
        RenderedSlice sliceMinc  = renderSlice(volMinc, params, 2, centerY);
        
        if (sliceNifti.pixels.empty() || sliceMinc.pixels.empty())
        {
            FAIL("rendered slice is empty");
        }
        else
        {
            int mm = compareSlices(sliceNifti, sliceMinc);
            if (mm < 0)
                FAIL("could not compare slices (size mismatch)");
            else if (mm > 0)
                FAIL(std::to_string(mm) + " pixel(s) differ from reference");
            else
                PASS();
        }
    }

    std::cerr << "\n" << testsPassed << " passed, " << testsFailed << " failed.\n";
    return testsFailed > 0 ? 1 : 0;
}
