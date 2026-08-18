#include "render/Layout.hpp"

#include <algorithm>

namespace mriv::term
{

namespace
{

/// Split `total` pixels into `count` tracks separated by `gap`, returning
/// each track's offset and size. The remainder of the division goes to the
/// last track so the tracks tile `total` exactly.
struct Track
{
    int offset;
    int size;
};

std::vector<Track> splitAxis(int total, int count, int gap)
{
    std::vector<Track> tracks;
    tracks.reserve(static_cast<size_t>(count));

    // A gutter that would leave nothing to draw in is dropped: a cramped
    // pane is still useful, an empty one is not.
    int totalGap = gap * (count - 1);
    if (totalGap >= total)
        gap = 0;

    int available = total - gap * (count - 1);
    int base = std::max(1, available / count);

    int offset = 0;
    for (int i = 0; i < count; ++i)
    {
        bool last = (i == count - 1);
        int size = last ? std::max(1, total - offset) : base;
        tracks.push_back(Track{offset, size});
        offset += size + gap;
    }

    return tracks;
}

} // namespace

std::vector<CellRect> computeGrid(int boxW, int boxH, int cols, int rows, int gap)
{
    if (boxW <= 0 || boxH <= 0 || cols <= 0 || rows <= 0)
        return {};

    auto columns = splitAxis(boxW, cols, gap);
    auto lines   = splitAxis(boxH, rows, gap);

    std::vector<CellRect> cells;
    cells.reserve(static_cast<size_t>(cols) * rows);

    for (const auto& line : lines)
    {
        for (const auto& column : columns)
            cells.push_back(CellRect{column.offset, line.offset, column.size, line.size});
    }

    return cells;
}

} // namespace mriv::term
