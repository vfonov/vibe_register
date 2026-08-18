#include "render/FrameBuilder.hpp"

#include "render/Compose.hpp"
#include "render/Layout.hpp"
#include "render/SlicePipeline.hpp"

namespace mriv::term
{

ResampledImage buildFrame(const FrameRequest& request)
{
    if (request.panes.empty() || request.views.empty())
        return ResampledImage{};

    int cols = static_cast<int>(request.panes.size());
    int rows = static_cast<int>(request.views.size());

    auto cells = computeGrid(request.boxWidth, request.boxHeight, cols, rows, request.gap);
    if (cells.empty())
        return ResampledImage{};

    // Held by value so the PlacedImage pointers stay valid until the
    // composite is done; reserved up front for the same reason.
    std::vector<ResampledImage> images;
    images.reserve(cells.size());
    std::vector<PlacedImage> placed;
    placed.reserve(cells.size());

    for (int row = 0; row < rows; ++row)
    {
        for (int col = 0; col < cols; ++col)
        {
            const FramePane& pane = request.panes[static_cast<size_t>(col)];
            const CellRect& rect  = cells[static_cast<size_t>(row) * cols + col];

            images.emplace_back();
            ResampledImage& image = images.back();

            if (pane.volume && static_cast<size_t>(row) < pane.sliceIndices.size())
            {
                SliceRequest slice;
                slice.viewIndex       = request.views[static_cast<size_t>(row)];
                slice.sliceIndex      = pane.sliceIndices[static_cast<size_t>(row)];
                slice.valueMin        = pane.valueMin;
                slice.valueMax        = pane.valueMax;
                slice.colourMap       = pane.colourMap;
                slice.invertColourMap = pane.invertColourMap;
                slice.maxWidth        = rect.w;
                slice.maxHeight       = rect.h;
                slice.scale           = request.scale;

                image = renderSliceForDisplay(*pane.volume, slice);
            }

            placed.push_back(PlacedImage{&image, rect});
        }
    }

    return composeGrid(placed, request.boxWidth, request.boxHeight);
}

} // namespace mriv::term
