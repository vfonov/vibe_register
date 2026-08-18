/// test_overlay.cpp — the text drawn around the image in interactive mode.
///
/// The grid is one bitmap, so nothing inside it can say which pane the keys
/// act on. The answer is terminal text placed beside and beneath it: the
/// volume names and the status row above, a '*' to the left of the view row
/// j/k moves, and a '*' under the column c and r change. Getting those two
/// markers onto the right cells is arithmetic over the frame's pane
/// geometry and the terminal's cell size, which is exactly the kind of
/// decision that must not live in Screen (mriv/HANDOFF.md sec 3.9).

#include <cassert>
#include <string>
#include <vector>

#include "interactive/Overlay.hpp"

using namespace mriv::term;

namespace
{

/// Two columns and two rows, with a gutter, at a plausible cell size.
FrameTracks makeTracks()
{
    FrameTracks tracks;
    tracks.columnX      = {0, 100};
    tracks.columnWidths = {80, 80};
    tracks.rowY         = {0, 60};
    tracks.rowHeights   = {50, 50};
    return tracks;
}

constexpr int kCellWidth  = 10;
constexpr int kCellHeight = 20;
constexpr int kImageHeight = 110;

const std::vector<std::string> kHeader{"1: a.mnc", "2: b.mnc", "axial (z) 5/50"};

/// The cell carrying `text`, or a row/col of -1 if it was not drawn.
TextCell find(const FrameOverlay& overlay, const std::string& text)
{
    for (const auto& cell : overlay.text)
        if (cell.text == text)
            return cell;
    return TextCell{-1, -1, std::string()};
}

int markerCount(const FrameOverlay& overlay)
{
    int count = 0;
    for (const auto& cell : overlay.text)
        if (cell.text == std::string(1, kMarker))
            ++count;
    return count;
}

/// The names go on top, one per row, with the status row last; the image
/// starts below them and is indented far enough for the row markers.
void testHeaderSitsAboveTheImage()
{
    auto overlay = planOverlay(kHeader, makeTracks(), 0, 0, kImageHeight,
                               kCellWidth, kCellHeight);

    assert(find(overlay, "1: a.mnc").row == 0);
    assert(find(overlay, "2: b.mnc").row == 1);
    assert(find(overlay, "axial (z) 5/50").row == 2);
    assert(find(overlay, "1: a.mnc").col == 0);

    assert(overlay.imageRow == 3);
    assert(overlay.imageColumn == kMarkerColumns);
    assert(kMarkerColumns >= 1);
}

/// The row marker sits in the left gutter, level with the middle of the
/// view j/k is moving -- not at its top edge, where it would read as
/// belonging to the row above.
void testRowMarkerPointsAtTheActiveView()
{
    auto first = planOverlay(kHeader, makeTracks(), 0, 0, kImageHeight,
                             kCellWidth, kCellHeight);
    // Row 0 spans y 0..50, centre 25, which is terminal row 1 of the image.
    TextCell marker{-1, -1, std::string()};
    for (const auto& cell : first.text)
        if (cell.text == std::string(1, kMarker) && cell.col == 0)
            marker = cell;
    assert(marker.row == first.imageRow + 1);

    auto second = planOverlay(kHeader, makeTracks(), 1, 0, kImageHeight,
                              kCellWidth, kCellHeight);
    for (const auto& cell : second.text)
        if (cell.text == std::string(1, kMarker) && cell.col == 0)
            marker = cell;
    // Row 1 spans y 60..110, centre 85, terminal row 4.
    assert(marker.row == second.imageRow + 4);
}

/// The column marker sits on the first row below the picture, centred on
/// the column c and r act on.
void testColumnMarkerSitsUnderTheActiveVolume()
{
    auto first = planOverlay(kHeader, makeTracks(), 0, 0, kImageHeight,
                             kCellWidth, kCellHeight);
    int footerRow = first.imageRow + 6; // 110px over 20px cells, rounded up

    TextCell marker{-1, -1, std::string()};
    for (const auto& cell : first.text)
        if (cell.text == std::string(1, kMarker) && cell.row == footerRow)
            marker = cell;
    // Column 0 spans x 0..80, centre 40, four cells in.
    assert(marker.col == first.imageColumn + 4);

    auto second = planOverlay(kHeader, makeTracks(), 0, 1, kImageHeight,
                              kCellWidth, kCellHeight);
    for (const auto& cell : second.text)
        if (cell.text == std::string(1, kMarker) && cell.row == footerRow)
            marker = cell;
    // Column 1 spans x 100..180, centre 140, fourteen cells in.
    assert(marker.col == second.imageColumn + 14);
}

/// Exactly two markers, always: one row, one column. A frame carrying more
/// would be pointing at panes the keys do not act on.
void testThereIsOneMarkerPerAxis()
{
    auto overlay = planOverlay(kHeader, makeTracks(), 1, 1, kImageHeight,
                               kCellWidth, kCellHeight);
    assert(markerCount(overlay) == 2);
}

/// A terminal that reports no cell size cannot be told where the panes are
/// on screen. The header is still worth drawing; a marker guessed from a
/// zero cell size would land anywhere.
void testUnknownCellSizeDropsTheMarkers()
{
    auto overlay = planOverlay(kHeader, makeTracks(), 0, 0, kImageHeight, 0, 0);
    assert(markerCount(overlay) == 0);
    assert(find(overlay, "1: a.mnc").row == 0);
    assert(overlay.imageRow == 3);
}

/// An index with no track behind it marks nothing rather than pointing off
/// the edge of the picture.
void testOutOfRangeIndicesMarkNothing()
{
    auto overlay = planOverlay(kHeader, makeTracks(), 9, 9, kImageHeight,
                               kCellWidth, kCellHeight);
    assert(markerCount(overlay) == 0);

    auto empty = planOverlay(kHeader, FrameTracks{}, 0, 0, kImageHeight,
                             kCellWidth, kCellHeight);
    assert(markerCount(empty) == 0);
}

/// No header still leaves room for the image at the top of the screen.
void testEmptyHeaderPutsTheImageFirst()
{
    auto overlay = planOverlay({}, makeTracks(), 0, 0, kImageHeight,
                               kCellWidth, kCellHeight);
    assert(overlay.imageRow == 0);
    assert(markerCount(overlay) == 2);
}

} // namespace

int main()
{
    testHeaderSitsAboveTheImage();
    testRowMarkerPointsAtTheActiveView();
    testColumnMarkerSitsUnderTheActiveVolume();
    testThereIsOneMarkerPerAxis();
    testUnknownCellSizeDropsTheMarkers();
    testOutOfRangeIndicesMarkNothing();
    testEmptyHeaderPutsTheImageFirst();
    return 0;
}
