#pragma once

#include <vector>

namespace mriv::term
{

/// A rectangle in the terminal's pixel box, in pixels from the top-left.
struct CellRect
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

/// Divide a display box into a grid of cells: one column per volume, one
/// row per view. Returns the cells in row-major order (index = row * cols +
/// col), so a caller can loop views outside and volumes inside without a
/// second index calculation.
///
/// `gap` is a gutter in pixels between neighbouring cells, taken out of the
/// cells rather than added to the box -- two volumes side by side must not
/// read as one wide image. The remainder of an uneven division goes to the
/// last row and column, so the cells always tile the box exactly instead of
/// leaving a strip of dead pixels at the far edge.
///
/// Returns an empty vector if the box or the grid is degenerate. Cells are
/// never smaller than 1x1, so a later resample can always divide by them.
std::vector<CellRect> computeGrid(int boxW, int boxH, int cols, int rows, int gap);

} // namespace mriv::term
