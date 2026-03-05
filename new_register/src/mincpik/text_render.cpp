/// text_render.cpp — Text rendering utilities for new_mincpik.

#include "text_render.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "stb_easy_font.h"

/// Parse a hex digit (0-9, a-f, A-F).  Returns -1 on invalid input.
static int hexDigit(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
    if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
    return -1;
}

uint32_t parseFgColour(const std::string& str)
{
    auto pack = [](uint8_t r, uint8_t g, uint8_t b) -> uint32_t
    {
        return static_cast<uint32_t>(r)
             | (static_cast<uint32_t>(g) << 8)
             | (static_cast<uint32_t>(b) << 16)
             | (0xFFu << 24);
    };

    // Named colours (case-insensitive)
    std::string lower = str;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (lower == "white")                    return pack(255, 255, 255);
    if (lower == "black")                    return pack(0, 0, 0);
    if (lower == "red")                      return pack(255, 0, 0);
    if (lower == "green")                    return pack(0, 255, 0);
    if (lower == "blue")                     return pack(0, 0, 255);
    if (lower == "yellow")                   return pack(255, 255, 0);
    if (lower == "cyan")                     return pack(0, 255, 255);
    if (lower == "magenta")                  return pack(255, 0, 255);
    if (lower == "gray" || lower == "grey")  return pack(128, 128, 128);
    if (lower == "orange")                   return pack(255, 165, 0);

    // Hex parsing
    std::string_view hex = str;
    if (!hex.empty() && hex[0] == '#')
        hex.remove_prefix(1);

    if (hex.size() == 6)
    {
        int r1 = hexDigit(hex[0]), r2 = hexDigit(hex[1]);
        int g1 = hexDigit(hex[2]), g2 = hexDigit(hex[3]);
        int b1 = hexDigit(hex[4]), b2 = hexDigit(hex[5]);
        if (r1 >= 0 && r2 >= 0 && g1 >= 0 && g2 >= 0 && b1 >= 0 && b2 >= 0)
        {
            return pack(static_cast<uint8_t>(r1 * 16 + r2),
                        static_cast<uint8_t>(g1 * 16 + g2),
                        static_cast<uint8_t>(b1 * 16 + b2));
        }
    }
    else if (hex.size() == 3)
    {
        int r = hexDigit(hex[0]);
        int g = hexDigit(hex[1]);
        int b = hexDigit(hex[2]);
        if (r >= 0 && g >= 0 && b >= 0)
        {
            return pack(static_cast<uint8_t>(r * 17),
                        static_cast<uint8_t>(g * 17),
                        static_cast<uint8_t>(b * 17));
        }
    }

    std::cerr << "Warning: could not parse colour '" << str
              << "', defaulting to white.\n";
    return pack(255, 255, 255);
}

RenderedSlice renderTextRow(
    const std::string& text,
    uint32_t fgColour,
    int scale)
{
    RenderedSlice result;
    if (text.empty() || scale < 1)
        return result;

    // Measure text at native size
    // stb_easy_font functions take char* (not const), so cast away const.
    int nativeW = stb_easy_font_width(const_cast<char*>(text.c_str()));
    int nativeH = stb_easy_font_height(const_cast<char*>(text.c_str()));
    if (nativeW <= 0 || nativeH <= 0)
        return result;

    // Add 1px padding on each side to avoid clipping
    nativeW += 2;
    nativeH += 2;

    // Generate quads
    // Budget ~270 bytes per character; allocate generously.
    std::vector<char> vbuf(text.size() * 300 + 1024);
    unsigned char color[4] = {255, 255, 255, 255};  // placeholder; we use fgColour when writing pixels
    int numQuads = stb_easy_font_print(
        1.0f, 1.0f,  // 1px offset for padding
        const_cast<char*>(text.c_str()),
        color,
        vbuf.data(),
        static_cast<int>(vbuf.size()));

    if (numQuads <= 0)
        return result;

    // Rasterize quads into a native-size buffer.
    // Each quad = 4 vertices, each vertex = 16 bytes: {float x, float y, float z, uint8[4] color}
    // Quads are axis-aligned rectangles — just find min/max x/y and fill.
    std::vector<uint32_t> nativeBuf(nativeW * nativeH, 0x00000000);  // transparent

    for (int q = 0; q < numQuads; ++q)
    {
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        for (int v = 0; v < 4; ++v)
        {
            int off = (q * 4 + v) * 16;
            float vx, vy;
            std::memcpy(&vx, &vbuf[off + 0], sizeof(float));
            std::memcpy(&vy, &vbuf[off + 4], sizeof(float));
            if (vx < minX) minX = vx;
            if (vx > maxX) maxX = vx;
            if (vy < minY) minY = vy;
            if (vy > maxY) maxY = vy;
        }

        int x0 = static_cast<int>(std::floor(minX));
        int y0 = static_cast<int>(std::floor(minY));
        int x1 = static_cast<int>(std::ceil(maxX));
        int y1 = static_cast<int>(std::ceil(maxY));

        x0 = std::clamp(x0, 0, nativeW);
        x1 = std::clamp(x1, 0, nativeW);
        y0 = std::clamp(y0, 0, nativeH);
        y1 = std::clamp(y1, 0, nativeH);

        for (int py = y0; py < y1; ++py)
            for (int px = x0; px < x1; ++px)
                nativeBuf[py * nativeW + px] = fgColour;
    }

    // Scale up by nearest-neighbour
    int outW = nativeW * scale;
    int outH = nativeH * scale;

    result.width  = outW;
    result.height = outH;
    result.pixels.resize(outW * outH);

    for (int y = 0; y < outH; ++y)
    {
        int srcY = y / scale;
        for (int x = 0; x < outW; ++x)
        {
            int srcX = x / scale;
            result.pixels[y * outW + x] = nativeBuf[srcY * nativeW + srcX];
        }
    }

    return result;
}
