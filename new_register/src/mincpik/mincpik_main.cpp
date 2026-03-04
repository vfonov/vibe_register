/// new_mincpik — headless mosaic image generator for MINC2 volumes.
///
/// Loads one or more MINC2 volumes, renders slices using the same CPU
/// compositing algorithm as new_register's overlay panel, arranges them
/// in a grid (rows = planes, columns = slices), and writes a PNG file.
///
/// Zero GPU / X11 / Wayland / GLFW / ImGui dependencies.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "cxxopts.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "AppConfig.h"
#include "ColourMap.h"
#include "SliceRenderer.h"
#include "Transform.h"
#include "Volume.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Parse a comma-separated list of doubles.
static std::vector<double> parseDoubleList(const std::string& str)
{
    std::vector<double> out;
    std::istringstream ss(str);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        if (!token.empty())
            out.push_back(std::stod(token));
    }
    return out;
}

/// Parse a comma-separated list of floats.
static std::vector<float> parseFloatList(const std::string& str)
{
    std::vector<float> out;
    std::istringstream ss(str);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        if (!token.empty())
            out.push_back(std::stof(token));
    }
    return out;
}

/// Convert world coordinate to voxel index along a given axis.
/// viewIndex: 0=axial(Z), 1=sagittal(X), 2=coronal(Y).
static int worldToSliceVoxel(const Volume& vol, int viewIndex, double worldCoord)
{
    // Build a world point with the coordinate on the relevant axis
    glm::dvec3 worldPt(0.0, 0.0, 0.0);
    if (viewIndex == 0)
        worldPt.z = worldCoord;
    else if (viewIndex == 1)
        worldPt.x = worldCoord;
    else
        worldPt.y = worldCoord;

    glm::ivec3 voxel;
    vol.transformWorldToVoxel(worldPt, voxel);

    int maxDim;
    int idx;
    if (viewIndex == 0)
    {
        idx = voxel.z;
        maxDim = vol.dimensions.z;
    }
    else if (viewIndex == 1)
    {
        idx = voxel.x;
        maxDim = vol.dimensions.x;
    }
    else
    {
        idx = voxel.y;
        maxDim = vol.dimensions.y;
    }
    return std::clamp(idx, 0, maxDim - 1);
}

/// Compute evenly spaced voxel indices for a given number of slices.
/// Distributes N slices evenly across the volume extent, avoiding the
/// very first and last slices (which are typically blank).
static std::vector<int> evenlySpacedSlices(const Volume& vol, int viewIndex, int count)
{
    int maxDim;
    if (viewIndex == 0)
        maxDim = vol.dimensions.z;
    else if (viewIndex == 1)
        maxDim = vol.dimensions.x;
    else
        maxDim = vol.dimensions.y;

    std::vector<int> result;
    if (count <= 0)
        return result;
    if (count == 1)
    {
        result.push_back(maxDim / 2);
        return result;
    }

    // Skip 10% at each edge to avoid blank slices
    int lo = std::max(1, static_cast<int>(maxDim * 0.1));
    int hi = std::min(maxDim - 2, static_cast<int>(maxDim * 0.9));
    if (hi <= lo)
    {
        lo = 0;
        hi = maxDim - 1;
    }
    double step = (count > 1) ? static_cast<double>(hi - lo) / (count - 1) : 0.0;
    for (int i = 0; i < count; ++i)
        result.push_back(lo + static_cast<int>(std::round(i * step)));

    return result;
}

// ---------------------------------------------------------------------------
// Mosaic assembly
// ---------------------------------------------------------------------------

/// Blit a RenderedSlice into a larger pixel buffer at (destX, destY).
static void blitSlice(
    const RenderedSlice& slice,
    std::vector<uint32_t>& dest,
    int destWidth,
    int destX,
    int destY)
{
    for (int y = 0; y < slice.height; ++y)
    {
        int srcOff = y * slice.width;
        int dstOff = (destY + y) * destWidth + destX;
        std::memcpy(&dest[dstOff], &slice.pixels[srcOff],
                     slice.width * sizeof(uint32_t));
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    try
    {
        cxxopts::Options opts("new_mincpik",
            "Headless mosaic image generator for MINC2 volumes");

        opts.add_options()
            // Volume display options
            ("G,gray",     "GrayScale colour map for next volume")
            ("H,hot",      "HotMetal colour map for next volume")
            ("S,spectral", "Spectral colour map for next volume")
            ("r,red",      "Red colour map for next volume")
            ("g,green",    "Green colour map for next volume")
            ("b,blue",     "Blue colour map for next volume")
            ("lut",        "Named colour map for next volume",
                           cxxopts::value<std::string>())
            ("range",      "Value range <min>,<max> for next volume",
                           cxxopts::value<std::string>())
            ("l,label",    "Mark next volume as label volume")
            ("L,labels",   "Label description file for next volume",
                           cxxopts::value<std::string>())

            // Slice selection
            ("axial",       "Number of axial slices (default: 1)",
                            cxxopts::value<int>()->default_value("1"))
            ("sagittal",    "Number of sagittal slices (default: 1)",
                            cxxopts::value<int>()->default_value("1"))
            ("coronal",     "Number of coronal slices (default: 1)",
                            cxxopts::value<int>()->default_value("1"))
            ("axial-at",    "World Z coordinates (comma-separated)",
                            cxxopts::value<std::string>())
            ("sagittal-at", "World X coordinates (comma-separated)",
                            cxxopts::value<std::string>())
            ("coronal-at",  "World Y coordinates (comma-separated)",
                            cxxopts::value<std::string>())

            // Output
            ("o,output",    "Output PNG path (required)",
                            cxxopts::value<std::string>())
            ("width",       "Scale output to this width in pixels",
                            cxxopts::value<int>())
            ("gap",         "Cell gap in pixels (default: 2)",
                            cxxopts::value<int>()->default_value("2"))

            // Transform
            ("t,tags",      "Load .tag file for registration",
                            cxxopts::value<std::string>())
            ("xfm",         "Load .xfm linear transform file",
                            cxxopts::value<std::string>())

            // Misc
            ("c,config",    "Load config.json",
                            cxxopts::value<std::string>())
            ("alpha",       "Per-volume overlay alpha (comma-separated)",
                            cxxopts::value<std::string>())
            ("d,debug",     "Enable debug output")
            ("h,help",      "Show this help message")

            // Positional
            ("positional",  "Volume files",
                            cxxopts::value<std::vector<std::string>>());

        opts.parse_positional({"positional"});
        auto result = opts.parse(argc, argv);

        if (result.count("help"))
        {
            std::cout << opts.help() << "\n"
                      << "Available colour maps (for --lut):\n";
            for (int cm = 0; cm < colourMapCount(); ++cm)
                std::cout << "  " << colourMapName(static_cast<ColourMapType>(cm)) << "\n";
            std::cout << "\nExample:\n"
                      << "  new_mincpik --gray vol1.mnc -r vol2.mnc --coronal 5 -o mosaic.png\n";
            return 0;
        }

        bool debug = result.count("debug") > 0;

        if (!result.count("output"))
        {
            std::cerr << "Error: --output (-o) is required.\n"
                      << "Run 'new_mincpik --help' for usage.\n";
            return 1;
        }
        std::string outputPath = result["output"].as<std::string>();

        // Collect volume files
        std::vector<std::string> volumeFiles;
        if (result.count("positional"))
            volumeFiles = result["positional"].as<std::vector<std::string>>();

        if (volumeFiles.empty())
        {
            std::cerr << "Error: no volume files specified.\n"
                      << "Run 'new_mincpik --help' for usage.\n";
            return 1;
        }

        // --- Parse per-volume flags from argv (same pattern as new_register) ---
        // We walk argv to associate --lut/--red/etc., --range, --label, --labels
        // with the next positional volume file.

        struct PerVolOpts
        {
            std::optional<ColourMapType> colourMap;
            std::optional<std::array<double, 2>> range;
            bool isLabel = false;
            std::optional<std::string> labelDescFile;
        };
        std::vector<PerVolOpts> perVolOpts(volumeFiles.size());

        {
            std::optional<ColourMapType> pendingLut;
            bool pendingLabel = false;
            std::optional<std::string> pendingLabelDesc;
            std::optional<double> pendingMin, pendingMax;
            int volIdx = 0;

            for (int i = 1; i < argc; ++i)
            {
                std::string_view arg = argv[i];

                // Skip known valued flags
                if (arg == "--lut" || arg == "-L" || arg == "--labels" ||
                    arg == "--range" || arg == "-o" || arg == "--output" ||
                    arg == "-c" || arg == "--config" || arg == "-t" || arg == "--tags" ||
                    arg == "--xfm" || arg == "--alpha" || arg == "--width" ||
                    arg == "--gap" || arg == "--axial" || arg == "--sagittal" ||
                    arg == "--coronal" || arg == "--axial-at" ||
                    arg == "--sagittal-at" || arg == "--coronal-at")
                {
                    // Handle LUT/labels/range specially; skip other valued args
                    if (arg == "--lut")
                    {
                        ++i;
                        auto cmOpt = colourMapByName(argv[i]);
                        if (cmOpt)
                            pendingLut = *cmOpt;
                        else
                        {
                            std::cerr << "Unknown colour map: " << argv[i] << "\n"
                                      << "Available maps:";
                            for (int cm = 0; cm < colourMapCount(); ++cm)
                                std::cerr << " " << colourMapName(static_cast<ColourMapType>(cm));
                            std::cerr << "\n";
                            return 1;
                        }
                    }
                    else if (arg == "-L" || arg == "--labels")
                    {
                        ++i;
                        pendingLabelDesc = argv[i];
                    }
                    else if (arg == "--range")
                    {
                        ++i;
                        auto vals = parseDoubleList(argv[i]);
                        if (vals.size() >= 2)
                        {
                            pendingMin = vals[0];
                            pendingMax = vals[1];
                        }
                    }
                    else
                    {
                        ++i;  // skip value
                    }
                    continue;
                }

                // Colour map shorthand flags
                if (arg == "-r" || arg == "--red")     { pendingLut = ColourMapType::Red; continue; }
                if (arg == "-g" || arg == "--green")   { pendingLut = ColourMapType::Green; continue; }
                if (arg == "-b" || arg == "--blue")    { pendingLut = ColourMapType::Blue; continue; }
                if (arg == "-G" || arg == "--gray")    { pendingLut = ColourMapType::GrayScale; continue; }
                if (arg == "-H" || arg == "--hot")     { pendingLut = ColourMapType::HotMetal; continue; }
                if (arg == "-S" || arg == "--spectral") { pendingLut = ColourMapType::Spectral; continue; }
                if (arg == "-l" || arg == "--label")   { pendingLabel = true; continue; }

                // Skip boolean flags
                if (arg == "-d" || arg == "--debug" || arg == "-h" || arg == "--help")
                    continue;

                // Must be a positional volume file
                if (volIdx < static_cast<int>(perVolOpts.size()))
                {
                    if (pendingLut)
                    {
                        perVolOpts[volIdx].colourMap = *pendingLut;
                        pendingLut.reset();
                    }
                    if (pendingLabel)
                    {
                        perVolOpts[volIdx].isLabel = true;
                        pendingLabel = false;
                    }
                    if (pendingLabelDesc)
                    {
                        perVolOpts[volIdx].labelDescFile = *pendingLabelDesc;
                        pendingLabelDesc.reset();
                    }
                    if (pendingMin && pendingMax)
                    {
                        perVolOpts[volIdx].range = std::array<double, 2>{*pendingMin, *pendingMax};
                        pendingMin.reset();
                        pendingMax.reset();
                    }
                    ++volIdx;
                }
            }
        }

        // --- Load volumes ---
        std::vector<Volume> volumes;
        volumes.reserve(volumeFiles.size());

        for (size_t i = 0; i < volumeFiles.size(); ++i)
        {
            Volume vol;
            if (debug)
                std::cerr << "[mincpik] Loading " << volumeFiles[i] << "...\n";
            vol.load(volumeFiles[i]);

            if (perVolOpts[i].isLabel)
                vol.setLabelVolume(true);
            if (perVolOpts[i].labelDescFile)
                vol.loadLabelDescriptionFile(*perVolOpts[i].labelDescFile);

            volumes.push_back(std::move(vol));
        }

        // --- Build per-volume render params ---
        std::vector<VolumeRenderParams> params(volumes.size());

        // Per-volume alpha from --alpha flag
        std::vector<float> alphas;
        if (result.count("alpha"))
            alphas = parseFloatList(result["alpha"].as<std::string>());

        for (size_t i = 0; i < volumes.size(); ++i)
        {
            params[i].valueMin = volumes[i].min_value;
            params[i].valueMax = volumes[i].max_value;
            params[i].colourMap = ColourMapType::GrayScale;
            params[i].overlayAlpha = 1.0f;

            if (perVolOpts[i].colourMap)
                params[i].colourMap = *perVolOpts[i].colourMap;
            else if (perVolOpts[i].isLabel)
                params[i].colourMap = ColourMapType::Viridis;
            if (perVolOpts[i].range)
            {
                params[i].valueMin = (*perVolOpts[i].range)[0];
                params[i].valueMax = (*perVolOpts[i].range)[1];
                // Below-range voxels should be transparent, not clamped to the
                // lowest LUT colour.  This matches new_register's behaviour.
                params[i].underColourMode = kSliceClampTransparent;
                params[i].overColourMode  = kSliceClampTransparent;
            }
            if (i < alphas.size())
                params[i].overlayAlpha = alphas[i];
        }

        // --- Config overrides ---
        if (result.count("config"))
        {
            try
            {
                AppConfig cfg = loadConfig(result["config"].as<std::string>());
                for (size_t i = 0; i < cfg.volumes.size() && i < volumes.size(); ++i)
                {
                    // Config provides defaults; CLI flags above override
                    if (!perVolOpts[i].colourMap)
                    {
                        auto cmOpt = colourMapByName(cfg.volumes[i].colourMap);
                        if (cmOpt)
                            params[i].colourMap = *cmOpt;
                    }
                    if (!perVolOpts[i].range)
                    {
                        if (cfg.volumes[i].valueMin)
                            params[i].valueMin = *cfg.volumes[i].valueMin;
                        if (cfg.volumes[i].valueMax)
                            params[i].valueMax = *cfg.volumes[i].valueMax;
                    }
                }
            }
            catch (const std::exception& e)
            {
                std::cerr << "Warning: config load failed: " << e.what() << "\n";
            }
        }

        // --- Transform (optional) ---
        TransformResult xfmResult;
        if (result.count("tags") && volumes.size() >= 2)
        {
            // Load tag file, compute transform
            TagWrapper tags;
            tags.load(result["tags"].as<std::string>());
            if (tags.tagCount() >= kMinPointsLinear)
            {
                auto vol1Tags = tags.points();
                auto vol2Tags = tags.points2();
                if (!vol2Tags.empty())
                {
                    xfmResult = computeTransform(vol1Tags, vol2Tags, TransformType::LSQ6);
                    if (debug && xfmResult.valid)
                        std::cerr << "[mincpik] Transform computed (LSQ6, "
                                  << tags.tagCount() << " tags, RMS="
                                  << xfmResult.avgRMS << ")\n";
                }
            }
        }
        if (result.count("xfm") && volumes.size() >= 2)
        {
            glm::dmat4 mat;
            if (readXfmFile(result["xfm"].as<std::string>(), mat))
            {
                xfmResult.valid = true;
                xfmResult.type = TransformType::LSQ12;
                xfmResult.linearMatrix = mat;
                if (debug)
                    std::cerr << "[mincpik] Loaded .xfm transform\n";
            }
            else
            {
                std::cerr << "Warning: failed to read .xfm file\n";
            }
        }

        // --- Determine slice coordinates ---
        // viewIndex: 0=axial(Z), 1=sagittal(X), 2=coronal(Y)
        // Each entry in sliceCoords[view] is a list of voxel indices.
        std::vector<int> sliceCoords[3];

        const Volume& refVol = volumes[0];

        // User-specified world coordinates take priority
        if (result.count("axial-at"))
        {
            auto coords = parseDoubleList(result["axial-at"].as<std::string>());
            for (double c : coords)
                sliceCoords[0].push_back(worldToSliceVoxel(refVol, 0, c));
        }
        if (result.count("sagittal-at"))
        {
            auto coords = parseDoubleList(result["sagittal-at"].as<std::string>());
            for (double c : coords)
                sliceCoords[1].push_back(worldToSliceVoxel(refVol, 1, c));
        }
        if (result.count("coronal-at"))
        {
            auto coords = parseDoubleList(result["coronal-at"].as<std::string>());
            for (double c : coords)
                sliceCoords[2].push_back(worldToSliceVoxel(refVol, 2, c));
        }

        // Fall back to evenly spaced slices
        int nAxial    = result["axial"].as<int>();
        int nSagittal = result["sagittal"].as<int>();
        int nCoronal  = result["coronal"].as<int>();

        if (sliceCoords[0].empty())
            sliceCoords[0] = evenlySpacedSlices(refVol, 0, nAxial);
        if (sliceCoords[1].empty())
            sliceCoords[1] = evenlySpacedSlices(refVol, 1, nSagittal);
        if (sliceCoords[2].empty())
            sliceCoords[2] = evenlySpacedSlices(refVol, 2, nCoronal);

        int gap = result["gap"].as<int>();

        // --- Render all slices ---
        // Layout: rows = planes (axial, sagittal, coronal)
        //         columns = slices within each plane
        // We only include rows that have at least one slice.

        struct SliceRow
        {
            int viewIndex;
            std::vector<RenderedSlice> slices;
        };
        std::vector<SliceRow> rows;

        // Order: coronal (view 2), sagittal (view 1), axial (view 0)
        // This matches the visual convention in PLAN.md
        int viewOrder[] = {2, 1, 0};

        bool useOverlay = (volumes.size() >= 2);

        for (int vi : viewOrder)
        {
            if (sliceCoords[vi].empty())
                continue;

            SliceRow row;
            row.viewIndex = vi;

            for (int sliceIdx : sliceCoords[vi])
            {
                if (useOverlay)
                {
                    std::vector<const Volume*> volPtrs;
                    for (auto& v : volumes)
                        volPtrs.push_back(&v);

                    const TransformResult* xfm = xfmResult.valid ? &xfmResult : nullptr;
                    row.slices.push_back(
                        renderOverlaySlice(volPtrs, params, vi, sliceIdx, xfm));
                }
                else
                {
                    row.slices.push_back(
                        renderSlice(volumes[0], params[0], vi, sliceIdx));
                }
            }

            rows.push_back(std::move(row));
        }

        if (rows.empty())
        {
            std::cerr << "Error: no slices to render.\n";
            return 1;
        }

        // --- Compute mosaic dimensions ---
        // Find the max number of columns across all rows
        int maxCols = 0;
        for (const auto& row : rows)
            maxCols = std::max(maxCols, static_cast<int>(row.slices.size()));

        // Compute total width and height
        // Each row's height is the max slice height in that row
        int totalWidth = 0;
        int totalHeight = 0;

        // First, find per-row dimensions
        struct RowLayout
        {
            int maxSliceWidth = 0;
            int maxSliceHeight = 0;
        };
        std::vector<RowLayout> rowLayouts(rows.size());

        for (size_t r = 0; r < rows.size(); ++r)
        {
            for (const auto& slice : rows[r].slices)
            {
                rowLayouts[r].maxSliceWidth = std::max(rowLayouts[r].maxSliceWidth, slice.width);
                rowLayouts[r].maxSliceHeight = std::max(rowLayouts[r].maxSliceHeight, slice.height);
            }
        }

        // Compute global max cell width (so columns are aligned)
        int cellWidth = 0;
        for (const auto& rl : rowLayouts)
            cellWidth = std::max(cellWidth, rl.maxSliceWidth);

        totalWidth = cellWidth * maxCols + gap * (maxCols - 1);
        for (size_t r = 0; r < rows.size(); ++r)
        {
            totalHeight += rowLayouts[r].maxSliceHeight;
            if (r + 1 < rows.size())
                totalHeight += gap;
        }

        if (debug)
        {
            std::cerr << "[mincpik] Mosaic: " << totalWidth << "x" << totalHeight
                      << " (" << rows.size() << " rows, " << maxCols << " cols)\n";
            for (size_t r = 0; r < rows.size(); ++r)
            {
                const char* viewNames[] = {"axial", "sagittal", "coronal"};
                std::cerr << "  Row " << r << ": " << viewNames[rows[r].viewIndex]
                          << " (" << rows[r].slices.size() << " slices, "
                          << rowLayouts[r].maxSliceWidth << "x"
                          << rowLayouts[r].maxSliceHeight << ")\n";
            }
        }

        // --- Assemble mosaic ---
        std::vector<uint32_t> mosaic(totalWidth * totalHeight, 0xFF000000);  // opaque black

        int curY = 0;
        for (size_t r = 0; r < rows.size(); ++r)
        {
            int curX = 0;
            for (size_t c = 0; c < rows[r].slices.size(); ++c)
            {
                const auto& slice = rows[r].slices[c];
                // Center slice within its cell
                int offsetX = curX + (cellWidth - slice.width) / 2;
                int offsetY = curY + (rowLayouts[r].maxSliceHeight - slice.height) / 2;
                blitSlice(slice, mosaic, totalWidth, offsetX, offsetY);
                curX += cellWidth + gap;
            }
            curY += rowLayouts[r].maxSliceHeight + gap;
        }

        // --- Optional width scaling ---
        std::vector<uint32_t> finalPixels;
        int finalW = totalWidth;
        int finalH = totalHeight;

        if (result.count("width"))
        {
            int targetW = result["width"].as<int>();
            if (targetW > 0 && targetW != totalWidth)
            {
                double scale = static_cast<double>(targetW) / totalWidth;
                finalW = targetW;
                finalH = static_cast<int>(std::round(totalHeight * scale));
                if (finalH < 1) finalH = 1;

                finalPixels.resize(finalW * finalH);
                // Nearest-neighbour downscale
                for (int y = 0; y < finalH; ++y)
                {
                    int srcY = static_cast<int>(y / scale);
                    if (srcY >= totalHeight) srcY = totalHeight - 1;
                    for (int x = 0; x < finalW; ++x)
                    {
                        int srcX = static_cast<int>(x / scale);
                        if (srcX >= totalWidth) srcX = totalWidth - 1;
                        finalPixels[y * finalW + x] = mosaic[srcY * totalWidth + srcX];
                    }
                }
            }
        }

        const uint32_t* outPixels = finalPixels.empty() ? mosaic.data() : finalPixels.data();

        // --- Write PNG ---
        int stride = finalW * 4;  // 4 bytes per pixel (RGBA)
        int ok = stbi_write_png(outputPath.c_str(), finalW, finalH, 4,
                                outPixels, stride);
        if (!ok)
        {
            std::cerr << "Error: failed to write " << outputPath << "\n";
            return 1;
        }

        if (debug)
            std::cerr << "[mincpik] Wrote " << outputPath << " ("
                      << finalW << "x" << finalH << ")\n";

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}
