/// colour_bar.cpp — Colour bar / legend rendering for new_mincpik.

#include "colour_bar.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "text_render.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Format a double value compactly (e.g. "0", "50.25", "100.5").
static std::string formatValue(double val)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4g", val);
    return buf;
}

/// Fill a rectangular region in a pixel buffer with a solid colour.
static void fillRect(std::vector<uint32_t>& buf, int bufW,
                     int x0, int y0, int w, int h, uint32_t colour)
{
    for (int y = y0; y < y0 + h; ++y)
    {
        if (y < 0) continue;
        for (int x = x0; x < x0 + w; ++x)
        {
            if (x < 0 || x >= bufW) continue;
            buf[y * bufW + x] = colour;
        }
    }
}

/// Blit a RenderedSlice into a buffer, writing only non-transparent pixels.
static void blitTransparent(const RenderedSlice& src,
                            std::vector<uint32_t>& dst, int dstW,
                            int destX, int destY)
{
    for (int y = 0; y < src.height; ++y)
    {
        int dy = destY + y;
        if (dy < 0) continue;
        for (int x = 0; x < src.width; ++x)
        {
            int dx = destX + x;
            if (dx < 0 || dx >= dstW) continue;
            uint32_t px = src.pixels[y * src.width + x];
            if ((px >> 24) != 0)
                dst[dy * dstW + dx] = px;
        }
    }
}

/// Pack RGBA bytes into the 0xAABBGGRR format used throughout the pipeline.
static uint32_t packRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return static_cast<uint32_t>(r)
         | (static_cast<uint32_t>(g) << 8)
         | (static_cast<uint32_t>(b) << 16)
         | (static_cast<uint32_t>(a) << 24);
}

// ---------------------------------------------------------------------------
// renderContinuousBar
// ---------------------------------------------------------------------------

RenderedSlice renderContinuousBar(
    const ColourLut& lut,
    double valueMin, double valueMax,
    int extent,
    uint32_t fgColour, int fontScale,
    bool horizontal)
{
    RenderedSlice result;
    if (extent <= 0 || fontScale < 1)
        return result;

    // Render the three tick labels
    std::string minStr = formatValue(valueMin);
    std::string midStr = formatValue((valueMin + valueMax) / 2.0);
    std::string maxStr = formatValue(valueMax);

    RenderedSlice minLabel = renderTextRow(minStr, fgColour, fontScale);
    RenderedSlice midLabel = renderTextRow(midStr, fgColour, fontScale);
    RenderedSlice maxLabel = renderTextRow(maxStr, fgColour, fontScale);

    int textH = std::max({minLabel.height, midLabel.height, maxLabel.height});
    int textMaxW = std::max({minLabel.width, midLabel.width, maxLabel.width});

    // Gradient strip thickness (perpendicular to primary axis)
    int stripThick = std::max(8, fontScale * 12);
    int pad = std::max(2, fontScale * 2);  // padding between strip and labels

    if (horizontal)
    {
        // Layout: gradient strip on top, labels below
        //
        //  [========= gradient =========]
        //  min       mid            max

        int gradW = extent;
        int gradH = stripThick;
        int totalW = std::max(gradW, textMaxW);
        int totalH = gradH + pad + textH;

        result.width = totalW;
        result.height = totalH;
        result.pixels.assign(totalW * totalH, 0x00000000);

        // Draw gradient strip (left = min = LUT[0], right = max = LUT[255])
        for (int x = 0; x < gradW; ++x)
        {
            int idx = (gradW > 1)
                ? static_cast<int>(static_cast<float>(x) / (gradW - 1) * 255.0f + 0.5f)
                : 128;
            idx = std::clamp(idx, 0, 255);
            uint32_t colour = lut.table[idx];
            for (int y = 0; y < gradH; ++y)
                result.pixels[y * totalW + x] = colour;
        }

        // Blit min label (left-aligned below gradient)
        blitTransparent(minLabel, result.pixels, totalW, 0, gradH + pad);

        // Blit mid label (centered below gradient)
        int midX = std::max(0, (gradW - midLabel.width) / 2);
        blitTransparent(midLabel, result.pixels, totalW, midX, gradH + pad);

        // Blit max label (right-aligned below gradient)
        int maxX = std::max(0, gradW - maxLabel.width);
        blitTransparent(maxLabel, result.pixels, totalW, maxX, gradH + pad);
    }
    else
    {
        // Layout: gradient strip on the left, labels on the right
        //
        //  [strip] max
        //  [strip] mid
        //  [strip] min

        int gradW = stripThick;
        int gradH = extent;
        int totalW = gradW + pad + textMaxW;
        int totalH = gradH;

        result.width = totalW;
        result.height = totalH;
        result.pixels.assign(totalW * totalH, 0x00000000);

        // Draw gradient strip (top = max = LUT[255], bottom = min = LUT[0])
        for (int y = 0; y < gradH; ++y)
        {
            int idx = (gradH > 1)
                ? static_cast<int>(static_cast<float>(gradH - 1 - y) / (gradH - 1) * 255.0f + 0.5f)
                : 128;
            idx = std::clamp(idx, 0, 255);
            uint32_t colour = lut.table[idx];
            for (int x = 0; x < gradW; ++x)
                result.pixels[y * totalW + x] = colour;
        }

        int labelX = gradW + pad;

        // Blit max label at the top
        blitTransparent(maxLabel, result.pixels, totalW, labelX, 0);

        // Blit mid label at the middle
        int midY = std::max(0, (gradH - midLabel.height) / 2);
        blitTransparent(midLabel, result.pixels, totalW, labelX, midY);

        // Blit min label at the bottom
        int minY = std::max(0, gradH - minLabel.height);
        blitTransparent(minLabel, result.pixels, totalW, labelX, minY);
    }

    return result;
}

// ---------------------------------------------------------------------------
// renderLabelBar
// ---------------------------------------------------------------------------

RenderedSlice renderLabelBar(
    const std::unordered_map<int, LabelInfo>& labelLUT,
    uint32_t fgColour, int fontScale,
    int maxWidth, int maxHeight,
    bool horizontal)
{
    RenderedSlice result;
    if (labelLUT.empty() || fontScale < 1)
        return result;

    // Sort labels by ID, skip background (ID 0)
    std::map<int, const LabelInfo*> sorted;
    for (const auto& kv : labelLUT)
    {
        if (kv.first == 0)
            continue;
        sorted[kv.first] = &kv.second;
    }
    if (sorted.empty())
        return result;

    // Swatch size = font line height
    int swatchSize = std::max(8, fontScale * 12);
    int pad = std::max(2, fontScale * 2);

    // Pre-render all label name texts
    struct LabelEntry
    {
        int id;
        const LabelInfo* info;
        RenderedSlice text;
    };
    std::vector<LabelEntry> entries;
    entries.reserve(sorted.size());

    for (const auto& kv : sorted)
    {
        LabelEntry e;
        e.id = kv.first;
        e.info = kv.second;
        std::string name = e.info->name.empty()
            ? ("Label " + std::to_string(e.id))
            : e.info->name;
        e.text = renderTextRow(name, fgColour, fontScale);
        entries.push_back(std::move(e));
    }

    if (horizontal)
    {
        // Flow left-to-right, wrapping to next row when exceeding maxWidth.
        // Each entry: [swatch] pad [text] pad

        int entryH = std::max(swatchSize, entries.empty() ? 0 : entries[0].text.height);

        // Compute row layout
        struct RowInfo
        {
            int startIdx;
            int endIdx;
        };
        std::vector<RowInfo> rows;

        int curX = 0;
        int rowStart = 0;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            int entryW = swatchSize + pad + entries[i].text.width + pad * 2;
            if (curX + entryW > maxWidth && curX > 0)
            {
                rows.push_back({rowStart, static_cast<int>(i)});
                rowStart = static_cast<int>(i);
                curX = 0;
            }
            curX += entryW;
        }
        rows.push_back({rowStart, static_cast<int>(entries.size())});

        int totalH = static_cast<int>(rows.size()) * (entryH + pad) - pad;
        if (totalH <= 0) totalH = entryH;

        // Clamp to maxHeight
        if (totalH > maxHeight)
            totalH = maxHeight;

        result.width = maxWidth;
        result.height = totalH;
        result.pixels.assign(result.width * result.height, 0x00000000);

        int curY = 0;
        for (const auto& row : rows)
        {
            if (curY + entryH > totalH)
                break;

            curX = 0;
            for (int i = row.startIdx; i < row.endIdx; ++i)
            {
                const auto& e = entries[i];

                // Draw swatch
                uint32_t swCol = packRGBA(e.info->r, e.info->g, e.info->b, e.info->a);
                int swY = curY + (entryH - swatchSize) / 2;
                fillRect(result.pixels, result.width,
                         curX, swY, swatchSize, swatchSize, swCol);

                // Blit text
                int textY = curY + (entryH - e.text.height) / 2;
                blitTransparent(e.text, result.pixels, result.width,
                                curX + swatchSize + pad, textY);

                curX += swatchSize + pad + e.text.width + pad * 2;
            }
            curY += entryH + pad;
        }
    }
    else
    {
        // Stack entries vertically, one per row.
        // Each entry: [swatch] pad [text]

        int totalH = 0;
        int totalW = 0;
        for (const auto& e : entries)
        {
            int entryH = std::max(swatchSize, e.text.height);
            totalH += entryH + pad;
            int entryW = swatchSize + pad + e.text.width;
            if (entryW > totalW) totalW = entryW;
        }
        totalH -= pad;  // remove trailing padding

        // Clamp to budget
        if (totalW > maxWidth) totalW = maxWidth;
        if (totalH > maxHeight) totalH = maxHeight;
        if (totalW <= 0 || totalH <= 0)
            return result;

        result.width = totalW;
        result.height = totalH;
        result.pixels.assign(result.width * result.height, 0x00000000);

        int curY = 0;
        for (const auto& e : entries)
        {
            int entryH = std::max(swatchSize, e.text.height);
            if (curY + entryH > totalH)
                break;

            // Draw swatch
            uint32_t swCol = packRGBA(e.info->r, e.info->g, e.info->b, e.info->a);
            int swY = curY + (entryH - swatchSize) / 2;
            fillRect(result.pixels, result.width,
                     0, swY, swatchSize, swatchSize, swCol);

            // Blit text
            int textY = curY + (entryH - e.text.height) / 2;
            blitTransparent(e.text, result.pixels, result.width,
                            swatchSize + pad, textY);

            curY += entryH + pad;
        }
    }

    return result;
}
