/// test_layout.cpp — the display grid: columns are volumes, rows are views.
///
/// The grid divides the terminal's pixel box, so an off-by-one here is a
/// row of dead pixels or an image clipped at the edge on every frame. The
/// cells must tile the box exactly, with no overlaps and no gaps beyond the
/// requested gutter.

#include <cassert>

#include "render/Layout.hpp"

using namespace mriv::term;

namespace
{

void testSingleCellFillsTheBox()
{
    auto cells = computeGrid(800, 600, 1, 1, 0);
    assert(cells.size() == 1);
    assert(cells[0].x == 0 && cells[0].y == 0);
    assert(cells[0].w == 800 && cells[0].h == 600);
}

/// Row-major: cell index = row * cols + col, so a caller can iterate views
/// outside and volumes inside without a second index calculation.
void testCellsAreRowMajor()
{
    auto cells = computeGrid(200, 300, 2, 3, 0);
    assert(cells.size() == 6);

    // Row 0 spans the full width in two columns.
    assert(cells[0].x == 0 && cells[0].y == 0);
    assert(cells[1].x == 100 && cells[1].y == 0);
    // Row 1 sits below row 0.
    assert(cells[2].x == 0 && cells[2].y == 100);
    assert(cells[3].x == 100 && cells[3].y == 100);
    // Row 2 last.
    assert(cells[4].y == 200);
    assert(cells[5].y == 200);
}

/// The remainder of an uneven division goes to the last row and column
/// rather than being dropped, so the cells always tile the box exactly.
void testRemainderGoesToTheLastRowAndColumn()
{
    auto cells = computeGrid(101, 100, 2, 3, 0);
    assert(cells.size() == 6);

    assert(cells[0].w == 50);
    assert(cells[1].w == 51);
    assert(cells[1].x == 50);

    // 100 / 3 = 33 remainder 1.
    assert(cells[0].h == 33);
    assert(cells[2].h == 33);
    assert(cells[4].h == 34);
    assert(cells[4].y == 66);

    // Bottom-right corner reaches the far edge of the box exactly.
    assert(cells[5].x + cells[5].w == 101);
    assert(cells[5].y + cells[5].h == 100);
}

/// A gutter separates the panes so two volumes side by side do not read as
/// one image. It is taken out of the cells, never added to the box.
void testGapShrinksCellsWithoutGrowingTheBox()
{
    auto cells = computeGrid(200, 100, 2, 1, 10);
    assert(cells.size() == 2);
    // One 10px gutter leaves 190 to divide between two columns.
    assert(cells[0].w == 95);
    assert(cells[1].w == 95);
    assert(cells[1].x == 105);
    assert(cells[1].x + cells[1].w == 200);
    assert(cells[0].h == 100);
}

/// Degenerate inputs must not produce a negative or zero-sized cell that a
/// later resample would divide by.
void testDegenerateInputsStayPositive()
{
    assert(computeGrid(0, 0, 1, 1, 0).empty());
    assert(computeGrid(100, 100, 0, 3, 0).empty());
    assert(computeGrid(100, 100, 2, 0, 0).empty());

    // A gap wider than the box cannot eat the cells entirely.
    auto squeezed = computeGrid(10, 10, 3, 3, 100);
    assert(squeezed.size() == 9);
    for (const auto& cell : squeezed)
    {
        assert(cell.w >= 1);
        assert(cell.h >= 1);
    }
}

} // namespace

int main()
{
    testSingleCellFillsTheBox();
    testCellsAreRowMajor();
    testRemainderGoesToTheLastRowAndColumn();
    testGapShrinksCellsWithoutGrowingTheBox();
    testDegenerateInputsStayPositive();
    return 0;
}
