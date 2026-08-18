#include "render/FrameBuilder.hpp"

#include <algorithm>

#include "render/Compose.hpp"
#include "render/Layout.hpp"
#include "render/SlicePipeline.hpp"

namespace mriv::term
{

namespace
{

/// Offsets and total extent for a set of tracks of the given sizes,
/// separated by `gap`.
std::vector<int> trackOffsets(const std::vector<int>& sizes, int gap)
{
    std::vector<int> offsets;
    offsets.reserve(sizes.size());

    int position = 0;
    for (int size : sizes)
    {
        offsets.push_back(position);
        position += size + gap;
    }

    return offsets;
}

int totalExtent(const std::vector<int>& sizes, int gap)
{
    int total = 0;
    for (int size : sizes)
        total += size;
    if (!sizes.empty())
        total += gap * (static_cast<int>(sizes.size()) - 1);
    return total;
}

} // namespace

ResampledImage buildFrame(const FrameRequest& request)
{
    if (request.panes.empty() || request.views.empty())
        return ResampledImage{};

    int cols = static_cast<int>(request.panes.size());
    int rows = static_cast<int>(request.views.size());

    // The grid first divides the box into per-cell *budgets*. Each slice is
    // fitted into its budget, and the frame is then sized to what those
    // fits actually produced rather than to the box: renderSliceForDisplay()
    // never upscales, so a box-sized canvas would surround a small volume
    // with a screen of black and blit megabytes of it every frame.
    // The gutter magnifies with the panes: --scale enlarges the picture,
    // and a gutter that stayed 4px while the panes tripled would make the
    // frame's proportions depend on the zoom level.
    int gap = request.gap * std::max(1, request.scale);

    auto budgets = computeGrid(request.boxWidth, request.boxHeight, cols, rows, gap);
    if (budgets.empty())
        return ResampledImage{};

    std::vector<ResampledImage> images(static_cast<size_t>(cols) * rows);

    // A cell that rendered nothing still occupies a minimal track, so its
    // column does not collapse and shift its neighbours sideways.
    std::vector<int> columnWidths(static_cast<size_t>(cols), 1);
    std::vector<int> rowHeights(static_cast<size_t>(rows), 1);

    for (int row = 0; row < rows; ++row)
    {
        for (int col = 0; col < cols; ++col)
        {
            size_t cell = static_cast<size_t>(row) * cols + col;
            const FramePane& pane = request.panes[static_cast<size_t>(col)];

            if (pane.volume && static_cast<size_t>(row) < pane.sliceIndices.size())
            {
                const CellRect& budget = budgets[cell];

                SliceRequest slice;
                slice.viewIndex       = request.views[static_cast<size_t>(row)];
                slice.sliceIndex      = pane.sliceIndices[static_cast<size_t>(row)];
                slice.valueMin        = pane.valueMin;
                slice.valueMax        = pane.valueMax;
                slice.colourMap       = pane.colourMap;
                slice.invertColourMap = pane.invertColourMap;
                slice.maxWidth        = budget.w;
                slice.maxHeight       = budget.h;
                slice.scale           = request.scale;

                images[cell] = renderSliceForDisplay(*pane.volume, slice);
            }

            columnWidths[static_cast<size_t>(col)] =
                std::max(columnWidths[static_cast<size_t>(col)], images[cell].width);
            rowHeights[static_cast<size_t>(row)] =
                std::max(rowHeights[static_cast<size_t>(row)], images[cell].height);
        }
    }

    auto columnX = trackOffsets(columnWidths, gap);
    auto rowY    = trackOffsets(rowHeights, gap);

    int width  = totalExtent(columnWidths, gap);
    int height = totalExtent(rowHeights, gap);

    std::vector<PlacedImage> placed;
    placed.reserve(images.size());
    for (int row = 0; row < rows; ++row)
    {
        for (int col = 0; col < cols; ++col)
        {
            size_t cell = static_cast<size_t>(row) * cols + col;
            placed.push_back(PlacedImage{&images[cell],
                                         CellRect{columnX[static_cast<size_t>(col)],
                                                  rowY[static_cast<size_t>(row)],
                                                  columnWidths[static_cast<size_t>(col)],
                                                  rowHeights[static_cast<size_t>(row)]}});
        }
    }

    return composeGrid(placed, width, height);
}

} // namespace mriv::term
