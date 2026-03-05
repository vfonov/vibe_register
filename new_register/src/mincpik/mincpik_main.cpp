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
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "AppConfig.h"
#include "ColourMap.h"
#include "SliceRenderer.h"
#include "Transform.h"
#include "Volume.h"

#include "mincpik_cli.h"
#include "colour_bar.h"
#include "mosaic.h"
#include "text_render.h"

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    try
    {
        auto parsed = parseArgs(argc, argv);
        if (!parsed)
            return 1;

        auto& args = *parsed;

        if (args.help)
        {
            printUsage();
            return 0;
        }

        bool debug = args.debug;

        if (args.outputPath.empty())
        {
            std::cerr << "Error: --output (-o) is required.\n"
                      << "Run 'new_mincpik --help' for usage.\n";
            return 1;
        }

        if (args.volumeFiles.empty())
        {
            std::cerr << "Error: no volume files specified.\n"
                      << "Run 'new_mincpik --help' for usage.\n";
            return 1;
        }

        // --- Load volumes ---
        std::vector<Volume> volumes;
        volumes.reserve(args.volumeFiles.size());

        for (size_t i = 0; i < args.volumeFiles.size(); ++i)
        {
            Volume vol;
            if (debug)
                std::cerr << "[mincpik] Loading " << args.volumeFiles[i] << "...\n";
            vol.load(args.volumeFiles[i]);

            if (args.perVolOpts[i].isLabel)
                vol.setLabelVolume(true);
            if (args.perVolOpts[i].labelDescFile)
                vol.loadLabelDescriptionFile(*args.perVolOpts[i].labelDescFile);

            volumes.push_back(std::move(vol));
        }

        // --- Build per-volume render params ---
        std::vector<VolumeRenderParams> params(volumes.size());

        // Per-volume alpha from --alpha flag
        std::vector<float> alphas;
        if (!args.alphaStr.empty())
            alphas = parseFloatList(args.alphaStr);

        for (size_t i = 0; i < volumes.size(); ++i)
        {
            params[i].valueMin = volumes[i].min_value;
            params[i].valueMax = volumes[i].max_value;
            params[i].colourMap = ColourMapType::GrayScale;
            params[i].overlayAlpha = 1.0f;

            if (args.perVolOpts[i].colourMap)
                params[i].colourMap = *args.perVolOpts[i].colourMap;
            else if (args.perVolOpts[i].isLabel)
                params[i].colourMap = ColourMapType::Viridis;
            if (args.perVolOpts[i].range)
            {
                params[i].valueMin = (*args.perVolOpts[i].range)[0];
                params[i].valueMax = (*args.perVolOpts[i].range)[1];
                // Below-range voxels should be transparent so the background
                // (or underlying volume) shows through.  Above-range voxels
                // are clamped to the highest LUT entry (table[255]).
                params[i].underColourMode = kSliceClampTransparent;
                params[i].overColourMode  = kSliceClampCurrent;
            }
            if (i < alphas.size())
                params[i].overlayAlpha = alphas[i];
        }

        // --- Config overrides ---
        if (!args.configPath.empty())
        {
            try
            {
                AppConfig cfg = loadConfig(args.configPath);
                for (size_t i = 0; i < cfg.volumes.size() && i < volumes.size(); ++i)
                {
                    // Config provides defaults; CLI flags above override
                    if (!args.perVolOpts[i].colourMap)
                    {
                        auto cmOpt = colourMapByName(cfg.volumes[i].colourMap);
                        if (cmOpt)
                            params[i].colourMap = *cmOpt;
                    }
                    if (!args.perVolOpts[i].range)
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
        if (!args.tagsPath.empty() && volumes.size() >= 2)
        {
            // Load tag file, compute transform
            TagWrapper tags;
            tags.load(args.tagsPath);
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
        if (!args.xfmPath.empty() && volumes.size() >= 2)
        {
            glm::dmat4 mat;
            if (readXfmFile(args.xfmPath, mat))
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
        if (!args.axialAt.empty())
        {
            auto coords = parseDoubleList(args.axialAt);
            for (double c : coords)
                sliceCoords[0].push_back(worldToSliceVoxel(refVol, 0, c));
        }
        if (!args.sagittalAt.empty())
        {
            auto coords = parseDoubleList(args.sagittalAt);
            for (double c : coords)
                sliceCoords[1].push_back(worldToSliceVoxel(refVol, 1, c));
        }
        if (!args.coronalAt.empty())
        {
            auto coords = parseDoubleList(args.coronalAt);
            for (double c : coords)
                sliceCoords[2].push_back(worldToSliceVoxel(refVol, 2, c));
        }

        // Fall back to evenly spaced slices
        if (sliceCoords[0].empty())
            sliceCoords[0] = evenlySpacedSlices(refVol, 0, args.nAxial);
        if (sliceCoords[1].empty())
            sliceCoords[1] = evenlySpacedSlices(refVol, 1, args.nSagittal);
        if (sliceCoords[2].empty())
            sliceCoords[2] = evenlySpacedSlices(refVol, 2, args.nCoronal);

        int gap = args.gap;
        int nRows = std::max(1, args.rows);

        // --- Render all slices ---
        // Layout: rows = planes (axial, sagittal, coronal)
        //         columns = slices within each plane
        // When --rows N is given, each plane's slices are split across N
        // sub-rows (ceiling division for uneven counts).
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

            // Render all slices for this plane first
            std::vector<RenderedSlice> allSlices;

            for (int sliceIdx : sliceCoords[vi])
            {
                RenderedSlice raw;
                if (useOverlay)
                {
                    std::vector<const Volume*> volPtrs;
                    for (auto& v : volumes)
                        volPtrs.push_back(&v);

                    const TransformResult* xfm = xfmResult.valid ? &xfmResult : nullptr;
                    raw = renderOverlaySlice(volPtrs, params, vi, sliceIdx, xfm);
                }
                else
                {
                    raw = renderSlice(volumes[0], params[0], vi, sliceIdx);
                }

                // Correct for non-uniform voxel spacing so that output
                // pixels are square in world space (matches new_register).
                allSlices.push_back(
                    resampleToPhysicalAspect(raw, refVol, vi));
            }

            // Split into nRows sub-rows
            int total = static_cast<int>(allSlices.size());
            int perRow = (total + nRows - 1) / nRows;  // ceiling division

            for (int r = 0; r < nRows; ++r)
            {
                int start = r * perRow;
                if (start >= total)
                    break;
                int end = std::min(start + perRow, total);

                SliceRow row;
                row.viewIndex = vi;
                for (int s = start; s < end; ++s)
                    row.slices.push_back(std::move(allSlices[s]));

                rows.push_back(std::move(row));
            }
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

        // --- Foreground colour and font scale (shared by title and bar) ---
        uint32_t fgColour = parseFgColour(args.fgColourStr);

        // Determine font scale: explicit --font-scale, or auto-size
        // to ~4% of mosaic height (clamped 1x-8x).
        int fontSc = 1;
        if (args.fontScale.has_value())
        {
            fontSc = std::max(1, *args.fontScale);
        }
        else
        {
            fontSc = std::max(1, std::min(8,
                static_cast<int>(std::round(totalHeight * 0.04 / 12.0))));
        }

        // --- Optional title annotation ---
        // Render the title text and prepend it to the mosaic, expanding
        // the buffer vertically at the top.
        if (!args.title.empty())
        {
            RenderedSlice titleSlice = renderTextRow(args.title, fgColour, fontSc);

            if (titleSlice.width > 0 && titleSlice.height > 0)
            {
                int titleRowHeight = titleSlice.height + gap;
                int newHeight = totalHeight + titleRowHeight;

                std::vector<uint32_t> newMosaic(totalWidth * newHeight, 0xFF000000);

                // Blit title, centered horizontally
                int titleX = std::max(0, (totalWidth - titleSlice.width) / 2);
                for (int y = 0; y < titleSlice.height; ++y)
                {
                    for (int x = 0; x < titleSlice.width && (titleX + x) < totalWidth; ++x)
                    {
                        uint32_t px = titleSlice.pixels[y * titleSlice.width + x];
                        // Only write non-transparent pixels (title bg is transparent)
                        if ((px >> 24) != 0)
                            newMosaic[y * totalWidth + titleX + x] = px;
                    }
                }

                // Blit original mosaic below the title row
                for (int y = 0; y < totalHeight; ++y)
                {
                    std::memcpy(
                        &newMosaic[(titleRowHeight + y) * totalWidth],
                        &mosaic[y * totalWidth],
                        totalWidth * sizeof(uint32_t));
                }

                mosaic = std::move(newMosaic);
                totalHeight = newHeight;

                if (debug)
                    std::cerr << "[mincpik] Title added: scale=" << fontSc
                              << " titleH=" << titleSlice.height
                              << " newMosaic=" << totalWidth << "x" << totalHeight << "\n";
            }
        }

        // --- Optional colour bar ---
        if (args.barSide != BarSide::None)
        {
            bool horizontal = (args.barSide == BarSide::Bottom);
            bool isLabel = volumes[0].isLabelVolume()
                        && !volumes[0].getLabelLUT().empty();

            RenderedSlice barSlice;

            if (isLabel)
            {
                int budgetW = horizontal ? totalWidth : static_cast<int>(totalWidth * 0.25);
                int budgetH = horizontal ? static_cast<int>(totalHeight * 0.25) : totalHeight;
                barSlice = renderLabelBar(
                    volumes[0].getLabelLUT(), fgColour, fontSc,
                    budgetW, budgetH, horizontal);
            }
            else
            {
                int extent = horizontal ? totalWidth : totalHeight;
                barSlice = renderContinuousBar(
                    colourMapLut(params[0].colourMap),
                    params[0].valueMin, params[0].valueMax,
                    extent, fgColour, fontSc, horizontal);
            }

            if (barSlice.width > 0 && barSlice.height > 0)
            {
                if (horizontal)
                {
                    // Append bar below the mosaic
                    int newHeight = totalHeight + gap + barSlice.height;
                    int newWidth = std::max(totalWidth, barSlice.width);

                    std::vector<uint32_t> newMosaic(newWidth * newHeight, 0xFF000000);

                    // Copy existing mosaic
                    for (int y = 0; y < totalHeight; ++y)
                    {
                        std::memcpy(
                            &newMosaic[y * newWidth],
                            &mosaic[y * totalWidth],
                            totalWidth * sizeof(uint32_t));
                    }

                    // Blit bar centered horizontally below
                    int barX = std::max(0, (newWidth - barSlice.width) / 2);
                    int barY = totalHeight + gap;
                    for (int y = 0; y < barSlice.height; ++y)
                    {
                        for (int x = 0; x < barSlice.width && (barX + x) < newWidth; ++x)
                        {
                            uint32_t px = barSlice.pixels[y * barSlice.width + x];
                            if ((px >> 24) != 0)
                                newMosaic[(barY + y) * newWidth + barX + x] = px;
                        }
                    }

                    mosaic = std::move(newMosaic);
                    totalWidth = newWidth;
                    totalHeight = newHeight;
                }
                else
                {
                    // Append bar to the right of the mosaic
                    int newWidth = totalWidth + gap + barSlice.width;
                    int newHeight = std::max(totalHeight, barSlice.height);

                    std::vector<uint32_t> newMosaic(newWidth * newHeight, 0xFF000000);

                    // Copy existing mosaic
                    for (int y = 0; y < totalHeight; ++y)
                    {
                        std::memcpy(
                            &newMosaic[y * newWidth],
                            &mosaic[y * totalWidth],
                            totalWidth * sizeof(uint32_t));
                    }

                    // Blit bar centered vertically on the right
                    int barX = totalWidth + gap;
                    int barY = std::max(0, (newHeight - barSlice.height) / 2);
                    for (int y = 0; y < barSlice.height; ++y)
                    {
                        for (int x = 0; x < barSlice.width && (barX + x) < newWidth; ++x)
                        {
                            uint32_t px = barSlice.pixels[y * barSlice.width + x];
                            if ((px >> 24) != 0)
                                newMosaic[(barY + y) * newWidth + barX + x] = px;
                        }
                    }

                    mosaic = std::move(newMosaic);
                    totalWidth = newWidth;
                    totalHeight = newHeight;
                }

                if (debug)
                    std::cerr << "[mincpik] Colour bar added ("
                              << (horizontal ? "bottom" : "right")
                              << "): " << barSlice.width << "x" << barSlice.height
                              << " -> " << totalWidth << "x" << totalHeight << "\n";
            }
        }

        // --- Optional width scaling ---
        std::vector<uint32_t> finalPixels;
        int finalW = totalWidth;
        int finalH = totalHeight;

        if (args.width.has_value())
        {
            int targetW = *args.width;
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
        int ok = stbi_write_png(args.outputPath.c_str(), finalW, finalH, 4,
                                outPixels, stride);
        if (!ok)
        {
            std::cerr << "Error: failed to write " << args.outputPath << "\n";
            return 1;
        }

        if (debug)
            std::cerr << "[mincpik] Wrote " << args.outputPath << " ("
                      << finalW << "x" << finalH << ")\n";

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}
