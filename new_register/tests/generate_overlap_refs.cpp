/// generate_overlap_refs.cpp — regenerate reference PNGs for test_overlap.
///
/// Usage: generate_overlap_refs <sq1.mnc> <sq2_tr.mnc> <out_dir>
///
/// Renders axial, sagittal, and coronal midslices of the sq1+sq2_tr overlay
/// (both GrayScale, alpha=0.5) and writes:
///   correct_overlap_ax.png, correct_overlap_sa.png, correct_overlap_co.png
///
/// Run this whenever the rendering parameters in test_overlap.cpp change,
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

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::cerr << "Usage: generate_overlap_refs <sq1.mnc> <sq2_tr.mnc> <out_dir>\n";
        return 1;
    }

    std::string vol1Path = argv[1];
    std::string vol2Path = argv[2];
    std::string outDir   = argv[3];
    if (!outDir.empty() && outDir.back() != '/')
        outDir += '/';

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
              << ", range=[" << vol2.min_value << "," << vol2.max_value << "])\n";

    std::vector<const Volume*> vols = { &vol1, &vol2 };

    std::vector<VolumeRenderParams> pars(2);
    pars[0].valueMin     = vol1.min_value;
    pars[0].valueMax     = vol1.max_value;
    pars[0].colourMap    = ColourMapType::GrayScale;
    pars[0].overlayAlpha = 0.5f;

    pars[1].valueMin     = vol2.min_value;
    pars[1].valueMax     = vol2.max_value;
    pars[1].colourMap    = ColourMapType::GrayScale;
    pars[1].overlayAlpha = 0.5f;

    auto saveSlice = [&](int viewIndex, int sliceIndex, const char* filename) -> bool
    {
        RenderedSlice s = renderOverlaySlice(vols, pars, viewIndex, sliceIndex, nullptr);
        if (s.pixels.empty())
        {
            std::cerr << "  [error] empty slice for " << filename << "\n";
            return false;
        }

        // Convert 0xAABBGGRR → interleaved RGBA bytes for stbi_write_png
        std::vector<unsigned char> rgba(s.width * s.height * 4);
        for (int i = 0; i < s.width * s.height; ++i)
        {
            uint32_t p = s.pixels[i];
            rgba[i * 4 + 0] = static_cast<unsigned char>((p >>  0) & 0xFF);  // R
            rgba[i * 4 + 1] = static_cast<unsigned char>((p >>  8) & 0xFF);  // G
            rgba[i * 4 + 2] = static_cast<unsigned char>((p >> 16) & 0xFF);  // B
            rgba[i * 4 + 3] = static_cast<unsigned char>((p >> 24) & 0xFF);  // A
        }

        std::string path = outDir + filename;
        if (!stbi_write_png(path.c_str(), s.width, s.height, 4,
                            rgba.data(), s.width * 4))
        {
            std::cerr << "  [error] failed to write " << path << "\n";
            return false;
        }
        std::cerr << "  wrote " << path
                  << " (" << s.width << "x" << s.height << ")\n";
        return true;
    };

    bool ok = true;
    ok &= saveSlice(0, vol1.dimensions.z / 2, "correct_overlap_ax.png");
    ok &= saveSlice(1, vol1.dimensions.x / 2, "correct_overlap_sa.png");
    ok &= saveSlice(2, vol1.dimensions.y / 2, "correct_overlap_co.png");

    return ok ? 0 : 1;
}
