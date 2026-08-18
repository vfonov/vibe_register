#include "interactive/Overlay.hpp"

namespace mriv::term
{

namespace
{

bool inRange(const std::vector<int>& track, int index)
{
    return index >= 0 && static_cast<size_t>(index) < track.size();
}

/// The cell containing the middle of a track running from `offset` for
/// `size` pixels.
int centreCell(int offset, int size, int cellSize)
{
    return (offset + size / 2) / cellSize;
}

} // namespace

FrameOverlay planOverlay(const std::vector<std::string>& header,
                         const FrameTracks& tracks,
                         int activeRow,
                         int activeColumn,
                         int imageHeight,
                         int cellWidth,
                         int cellHeight)
{
    FrameOverlay overlay;
    overlay.imageRow    = static_cast<int>(header.size());
    overlay.imageColumn = kMarkerColumns;

    overlay.text.reserve(header.size() + 2);
    for (size_t i = 0; i < header.size(); ++i)
        overlay.text.push_back(TextCell{static_cast<int>(i), 0, header[i]});

    // Without a cell size there is no way to turn pane pixels into cells,
    // so the markers are dropped rather than placed at a guess.
    if (cellWidth <= 0 || cellHeight <= 0)
        return overlay;

    const std::string marker(1, kMarker);

    if (inRange(tracks.rowY, activeRow) && inRange(tracks.rowHeights, activeRow))
    {
        size_t row = static_cast<size_t>(activeRow);
        overlay.text.push_back(TextCell{
            overlay.imageRow + centreCell(tracks.rowY[row], tracks.rowHeights[row], cellHeight),
            0,
            marker});
    }

    if (inRange(tracks.columnX, activeColumn) && inRange(tracks.columnWidths, activeColumn))
    {
        size_t col = static_cast<size_t>(activeColumn);
        // The first whole row below the picture: a marker sharing a row
        // with the image's last pixels would be drawn over by the bitmap.
        int footerRow = overlay.imageRow + (imageHeight + cellHeight - 1) / cellHeight;
        overlay.text.push_back(TextCell{
            footerRow,
            overlay.imageColumn
                + centreCell(tracks.columnX[col], tracks.columnWidths[col], cellWidth),
            marker});
    }

    return overlay;
}

} // namespace mriv::term
