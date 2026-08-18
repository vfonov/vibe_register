#include "render/Compose.hpp"

#include <algorithm>

namespace mriv::term
{

namespace
{

constexpr uint32_t kOpaqueBlack = 0xff000000u;

} // namespace

ResampledImage composeGrid(const std::vector<PlacedImage>& cells, int width, int height)
{
    ResampledImage canvas;
    if (width <= 0 || height <= 0)
        return canvas;

    canvas.width  = width;
    canvas.height = height;
    canvas.pixels.assign(static_cast<size_t>(width) * height, kOpaqueBlack);

    for (const auto& cell : cells)
    {
        if (!cell.image || cell.image->width <= 0 || cell.image->height <= 0)
            continue;

        const ResampledImage& image = *cell.image;

        // Centred in the cell, and never further left or up than the cell's
        // own corner: an image wider than its cell is clipped rather than
        // allowed to run over its neighbour.
        int originX = cell.rect.x + std::max(0, (cell.rect.w - image.width) / 2);
        int originY = cell.rect.y + std::max(0, (cell.rect.h - image.height) / 2);

        int copyW = std::min(image.width, cell.rect.w);
        int copyH = std::min(image.height, cell.rect.h);

        // Clip against the canvas too, so a rect the caller placed partly
        // off the edge cannot write past the buffer.
        copyW = std::min(copyW, width - originX);
        copyH = std::min(copyH, height - originY);

        for (int y = 0; y < copyH; ++y)
        {
            const uint32_t* srcRow = image.pixels.data() + static_cast<size_t>(y) * image.width;
            uint32_t* dstRow = canvas.pixels.data()
                               + static_cast<size_t>(originY + y) * width + originX;
            std::copy(srcRow, srcRow + copyW, dstRow);
        }
    }

    return canvas;
}

} // namespace mriv::term
