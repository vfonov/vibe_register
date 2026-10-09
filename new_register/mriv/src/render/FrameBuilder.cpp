#include "render/FrameBuilder.hpp"

#include <algorithm>

#include "render/Compose.hpp"
#include "render/Crosshair.hpp"
#include "render/Layout.hpp"
#include "render/SliceGeometry.hpp"
#include "render/SlicePipeline.hpp"

#include "Volume.h"

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

ResampledImage buildFrame(const FrameRequest& request, FrameTracks* tracks)
{
    if (tracks)
        *tracks = FrameTracks{};

    if (request.panes.empty() || request.views.empty())
        return ResampledImage{};

    int paneCount = static_cast<int>(request.panes.size());
    int rows      = static_cast<int>(request.views.size());

    // The overlay is one more column after the volumes' own. One volume has
    // nothing to blend with, so the switch is ignored rather than adding a
    // column that could only repeat the first.
    const bool overlay = request.showOverlay && paneCount >= 2;
    int cols = paneCount + (overlay ? 1 : 0);

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

    // What the overlay column blends: every pane's volume with that pane's
    // own range and colour map. A pane that has no volume leaves the
    // overlay blank rather than quietly blending fewer volumes than the
    // columns beside it show.
    std::vector<const Volume*> overlayVolumes;
    std::vector<VolumeRenderParams> overlayParams;
    if (overlay)
    {
        for (const FramePane& pane : request.panes)
        {
            VolumeRenderParams params;
            params.valueMin        = pane.valueMin;
            params.valueMax        = pane.valueMax;
            params.colourMap       = pane.colourMap;
            params.invertColourMap = pane.invertColourMap;
            overlayVolumes.push_back(pane.volume);
            overlayParams.push_back(params);
        }
    }

    for (int row = 0; row < rows; ++row)
    {
        for (int col = 0; col < cols; ++col)
        {
            size_t cell = static_cast<size_t>(row) * cols + col;

            // The overlay column is drawn in the first pane's grid, so it
            // takes that pane's slice, crosshair and dimensions.
            const bool overlayCell = col >= paneCount;
            const FramePane& pane = request.panes[overlayCell ? 0 : static_cast<size_t>(col)];

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

                images[cell] = overlayCell
                                   ? renderOverlayForDisplay(overlayVolumes, overlayParams, slice)
                                   : renderSliceForDisplay(*pane.volume, slice);

                if (images[cell].width > 0
                    && static_cast<size_t>(row) < pane.crosshairs.size())
                {
                    AspectAxes axes = aspectAxesForView(slice.viewIndex);
                    const glm::ivec2& voxel = pane.crosshairs[static_cast<size_t>(row)];
                    CrosshairMark mark{voxel.x, voxel.y,
                                       pane.volume->dimensions[axes.u],
                                       pane.volume->dimensions[axes.v]};
                    drawCrosshair(images[cell], mark);
                }
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

    if (tracks)
    {
        tracks->columnX      = columnX;
        tracks->columnWidths = columnWidths;
        tracks->rowY         = rowY;
        tracks->rowHeights   = rowHeights;
    }

    return composeGrid(placed, width, height);
}

} // namespace mriv::term
