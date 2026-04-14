/// generate_test_mode_refs.cpp — regenerate reference PNGs for test_test_mode.
///
/// Usage: generate_test_mode_refs <out_dir>
///
/// Generates the synthetic test volume (same data as new_register --test) and
/// writes six reference PNG slices to <out_dir>:
///   testmode_ax_gray.png   axial    centre, GrayScale
///   testmode_sa_gray.png   sagittal centre, GrayScale
///   testmode_co_gray.png   coronal  centre, GrayScale
///   testmode_ax_hot.png    axial    centre, HotMetal
///   testmode_sa_hot.png    sagittal centre, HotMetal
///   testmode_co_hot.png    coronal  centre, HotMetal
///
/// Run this whenever Volume::generate_test_data() or the colour-map LUTs change,
/// then commit the updated PNGs.

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "ColourMap.h"
#include "SliceRenderer.h"
#include "Volume.h"

static bool saveSlice(const RenderedSlice& s, const std::string& path)
{
    if (s.pixels.empty())
    {
        std::cerr << "  [error] empty slice for " << path << "\n";
        return false;
    }

    // Convert packed 0xAABBGGRR → interleaved RGBA bytes for stbi_write_png
    std::vector<unsigned char> rgba(s.width * s.height * 4);
    for (int i = 0; i < s.width * s.height; ++i)
    {
        uint32_t p = s.pixels[i];
        rgba[i * 4 + 0] = static_cast<unsigned char>((p >>  0) & 0xFF);  // R
        rgba[i * 4 + 1] = static_cast<unsigned char>((p >>  8) & 0xFF);  // G
        rgba[i * 4 + 2] = static_cast<unsigned char>((p >> 16) & 0xFF);  // B
        rgba[i * 4 + 3] = static_cast<unsigned char>((p >> 24) & 0xFF);  // A
    }

    if (!stbi_write_png(path.c_str(), s.width, s.height, 4, rgba.data(), s.width * 4))
    {
        std::cerr << "  [error] failed to write " << path << "\n";
        return false;
    }
    std::cerr << "  wrote " << path << " (" << s.width << "x" << s.height << ")\n";
    return true;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: generate_test_mode_refs <out_dir>\n";
        return 1;
    }

    std::string outDir = argv[1];
    if (!outDir.empty() && outDir.back() != '/')
        outDir += '/';

    Volume vol;
    vol.generate_test_data();
    std::cerr << "Generated test volume: "
              << vol.dimensions.x << "x" << vol.dimensions.y << "x" << vol.dimensions.z
              << ", range=[" << vol.min_value << "," << vol.max_value << "]\n";

    VolumeRenderParams pGray;
    pGray.valueMin  = vol.min_value;
    pGray.valueMax  = vol.max_value;
    pGray.colourMap = ColourMapType::GrayScale;

    VolumeRenderParams pHot;
    pHot.valueMin  = vol.min_value;
    pHot.valueMax  = vol.max_value;
    pHot.colourMap = ColourMapType::HotMetal;

    int cx = vol.dimensions.x / 2;
    int cy = vol.dimensions.y / 2;
    int cz = vol.dimensions.z / 2;

    bool ok = true;
    ok &= saveSlice(renderSlice(vol, pGray, 0, cz), outDir + "testmode_ax_gray.png");
    ok &= saveSlice(renderSlice(vol, pGray, 1, cx), outDir + "testmode_sa_gray.png");
    ok &= saveSlice(renderSlice(vol, pGray, 2, cy), outDir + "testmode_co_gray.png");
    ok &= saveSlice(renderSlice(vol, pHot,  0, cz), outDir + "testmode_ax_hot.png");
    ok &= saveSlice(renderSlice(vol, pHot,  1, cx), outDir + "testmode_sa_hot.png");
    ok &= saveSlice(renderSlice(vol, pHot,  2, cy), outDir + "testmode_co_hot.png");

    return ok ? 0 : 1;
}
