#include "render/Screenshot.hpp"

#include <cstdio>
#include <filesystem>
#include <iostream>

#include "stb_image_write.h"

namespace mriv::term
{

namespace
{

namespace fs = std::filesystem;

} // namespace

std::string nextScreenshotFilename(const std::string& dir)
{
    // Matches new_register's Interface::saveScreenshot() bit for bit: start
    // at 1, keep counting past any name already on disk (never fill a gap
    // left by a deleted screenshot), zero-padded to 6 digits.
    int index = 1;
    for (;;)
    {
        char nameBuf[32];
        std::snprintf(nameBuf, sizeof(nameBuf), "screenshot%06d.png", index);

        fs::path candidate = (dir.empty() || dir == ".")
            ? fs::path(nameBuf)
            : fs::path(dir) / nameBuf;

        if (!fs::exists(candidate))
            return candidate.string();
        ++index;
    }
}

std::string saveScreenshot(const std::uint32_t* rgba, int width, int height,
                           const std::string& dir)
{
    if (!rgba || width <= 0 || height <= 0)
    {
        std::cerr << "mriv: screenshot failed -- no frame to save\n";
        return {};
    }

    std::string path = nextScreenshotFilename(dir);

    // The packed 0xAABBGGRR layout already matches stb's row-major RGBA8
    // input on little-endian (render/Encode.cpp does the same reinterpret
    // for the Kitty protocol payload).
    int ok = stbi_write_png(path.c_str(), width, height, 4,
                            reinterpret_cast<const std::uint8_t*>(rgba),
                            width * 4);
    if (!ok)
    {
        std::cerr << "mriv: screenshot failed to write " << path << "\n";
        return {};
    }

    return path;
}

} // namespace mriv::term
