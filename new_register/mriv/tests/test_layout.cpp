/// test_layout.cpp — the display grid: columns are volumes, rows are views.
///
/// The grid divides the terminal's pixel box, so an off-by-one here is a
/// row of dead pixels or an image clipped at the edge on every frame. The
/// cells must tile the box exactly, with no overlaps and no gaps beyond the
/// requested gutter.

#include <catch2/catch_test_macros.hpp>

#include "render/Layout.hpp"

using namespace mriv::term;

TEST_CASE("a single cell fills the box", "[layout]")
{
    auto cells = computeGrid(800, 600, 1, 1, 0);
    REQUIRE(cells.size() == 1);
    CHECK(cells[0].x == 0);
    CHECK(cells[0].y == 0);
    CHECK(cells[0].w == 800);
    CHECK(cells[0].h == 600);
}

/// Row-major: cell index = row * cols + col, so a caller can iterate views
/// outside and volumes inside without a second index calculation.
TEST_CASE("cells are row-major", "[layout]")
{
    auto cells = computeGrid(200, 300, 2, 3, 0);
    REQUIRE(cells.size() == 6);

    // Row 0 spans the full width in two columns.
    CHECK(cells[0].x == 0);
    CHECK(cells[0].y == 0);
    CHECK(cells[1].x == 100);
    CHECK(cells[1].y == 0);
    // Row 1 sits below row 0.
    CHECK(cells[2].x == 0);
    CHECK(cells[2].y == 100);
    CHECK(cells[3].x == 100);
    CHECK(cells[3].y == 100);
    // Row 2 last.
    CHECK(cells[4].y == 200);
    CHECK(cells[5].y == 200);
}

/// The remainder of an uneven division goes to the last row and column
/// rather than being dropped, so the cells always tile the box exactly.
TEST_CASE("the remainder goes to the last row and column", "[layout]")
{
    auto cells = computeGrid(101, 100, 2, 3, 0);
    REQUIRE(cells.size() == 6);

    CHECK(cells[0].w == 50);
    CHECK(cells[1].w == 51);
    CHECK(cells[1].x == 50);

    // 100 / 3 = 33 remainder 1.
    CHECK(cells[0].h == 33);
    CHECK(cells[2].h == 33);
    CHECK(cells[4].h == 34);
    CHECK(cells[4].y == 66);

    // Bottom-right corner reaches the far edge of the box exactly.
    CHECK(cells[5].x + cells[5].w == 101);
    CHECK(cells[5].y + cells[5].h == 100);
}

/// A gutter separates the panes so two volumes side by side do not read as
/// one image. It is taken out of the cells, never added to the box.
TEST_CASE("a gap shrinks cells without growing the box", "[layout]")
{
    auto cells = computeGrid(200, 100, 2, 1, 10);
    REQUIRE(cells.size() == 2);
    // One 10px gutter leaves 190 to divide between two columns.
    CHECK(cells[0].w == 95);
    CHECK(cells[1].w == 95);
    CHECK(cells[1].x == 105);
    CHECK(cells[1].x + cells[1].w == 200);
    CHECK(cells[0].h == 100);
}

/// Degenerate inputs must not produce a negative or zero-sized cell that a
/// later resample would divide by.
TEST_CASE("degenerate inputs stay positive", "[layout]")
{
    CHECK(computeGrid(0, 0, 1, 1, 0).empty());
    CHECK(computeGrid(100, 100, 0, 3, 0).empty());
    CHECK(computeGrid(100, 100, 2, 0, 0).empty());

    // A gap wider than the box cannot eat the cells entirely.
    auto squeezed = computeGrid(10, 10, 3, 3, 100);
    REQUIRE(squeezed.size() == 9);
    for (const auto& cell : squeezed)
    {
        CHECK(cell.w >= 1);
        CHECK(cell.h >= 1);
    }
}
