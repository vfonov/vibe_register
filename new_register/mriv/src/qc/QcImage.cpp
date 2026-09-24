#include "qc/QcImage.hpp"

#include <algorithm>
#include <cstring>

// The one stb_image implementation in mriv_qc_lib. mriv/tests/decode.cpp has
// another, which is why the qc tests do not link mriv_test_support.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace mriv::qc
{

std::optional<Image> loadImage(const std::string& path)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    // Same call new_qc makes (QCApp::loadImage): force 4 channels, so the
    // bytes come out R,G,B,A -- 0xAABBGGRR once read as uint32_t on a
    // little-endian host, which is what mriv_term takes.
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data)
        return std::nullopt;

    Image image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
    std::memcpy(image.pixels.data(), data, image.pixels.size() * sizeof(uint32_t));
    stbi_image_free(data);
    return image;
}

mriv::term::ResampledImage fitImageToBox(const Image& image, int boxW, int boxH, int scale)
{
    if (image.width <= 0 || image.height <= 0 || boxW <= 0 || boxH <= 0)
        return {};

    int magnify = scale;
    if (magnify <= 0)
        magnify = std::max(1, std::min(boxW / image.width, boxH / image.height));

    // Square pixels: a 2D picture has no voxel aspect to correct.
    return mriv::term::resampleToDisplay(image.pixels.data(), image.width, image.height,
                                         1.0, boxW, boxH, magnify);
}

int centreOffsetCells(int boxPx, int imagePx, int cellPx)
{
    if (cellPx <= 0 || imagePx >= boxPx)
        return 0;

    int boxCells = boxPx / cellPx;
    int imageCells = (imagePx + cellPx - 1) / cellPx;
    return std::max(0, (boxCells - imageCells) / 2);
}

} // namespace mriv::qc
