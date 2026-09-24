// mriv_qc image loading and fitting: decode PNG/JPG to packed RGBA, then fit
// the picture into the terminal's pixel box.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "qc/QcImage.hpp"
#include "render/Screenshot.hpp"

namespace fs = std::filesystem;
using namespace mriv::qc;

namespace
{

/// A fresh, empty scratch directory under the system temp dir.
fs::path scratchDir(const std::string& name)
{
    fs::path dir = fs::temp_directory_path() / ("mriv_test_qc_image_" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("loadImage decodes a PNG into packed 0xAABBGGRR pixels", "[qc][image]")
{
    fs::path dir = scratchDir("png");
    // Written through mriv_term's own PNG writer: red, green / blue, white.
    const std::vector<uint32_t> rgba{0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFFFFFFFFu};
    std::string path = mriv::term::saveScreenshot(rgba.data(), 2, 2, dir.string());
    REQUIRE_FALSE(path.empty());

    auto image = loadImage(path);
    REQUIRE(image.has_value());
    CHECK(image->width == 2);
    CHECK(image->height == 2);
    CHECK(image->pixels == rgba);

    fs::remove_all(dir);
}

TEST_CASE("loadImage returns nullopt for a missing or undecodable file", "[qc][image]")
{
    fs::path dir = scratchDir("bad");
    CHECK_FALSE(loadImage((dir / "missing.png").string()).has_value());

    fs::path junk = dir / "junk.png";
    std::ofstream(junk) << "not an image";
    CHECK_FALSE(loadImage(junk.string()).has_value());

    fs::remove_all(dir);
}

TEST_CASE("fitImageToBox magnifies a small image by the largest integer that fits", "[qc][image]")
{
    Image image;
    image.width = 2;
    image.height = 2;
    image.pixels = {1, 2, 3, 4};

    // 100x60 box: 50x across, 30x down -> 30x, a 60x60 picture.
    auto out = fitImageToBox(image, 100, 60, 0);
    CHECK(out.width == 60);
    CHECK(out.height == 60);
    CHECK(out.pixels[0] == 1u);
    CHECK(out.pixels[out.width - 1] == 2u);
    CHECK(out.pixels.back() == 4u);
}

TEST_CASE("fitImageToBox shrinks a large image and keeps its aspect", "[qc][image]")
{
    Image image;
    image.width = 400;
    image.height = 200;
    image.pixels.assign(400u * 200u, 0xFFFFFFFFu);

    auto out = fitImageToBox(image, 100, 100, 0);
    CHECK(out.width == 100);
    CHECK(out.height == 50);
}

TEST_CASE("fitImageToBox honours an explicit --scale instead of auto-fitting", "[qc][image]")
{
    Image image;
    image.width = 2;
    image.height = 2;
    image.pixels = {1, 2, 3, 4};

    auto out = fitImageToBox(image, 100, 60, 3);
    CHECK(out.width == 6);
    CHECK(out.height == 6);
}

TEST_CASE("fitImageToBox gives an empty image for an empty source or box", "[qc][image]")
{
    Image empty;
    CHECK(fitImageToBox(empty, 100, 100, 0).pixels.empty());

    Image image;
    image.width = 2;
    image.height = 2;
    image.pixels = {1, 2, 3, 4};
    CHECK(fitImageToBox(image, 0, 100, 0).pixels.empty());
}

TEST_CASE("centreOffsetCells centres an image in whole terminal cells", "[qc][image]")
{
    // 800 px box, 400 px image, 10 px cells: 80 cells, image 40 -> offset 20.
    CHECK(centreOffsetCells(800, 400, 10) == 20);
    // Image partially covering a cell counts as the whole cell: 45 px -> 5 cells.
    CHECK(centreOffsetCells(100, 45, 10) == 2);
    // Image as wide as the box, or wider, or no cell size: no offset.
    CHECK(centreOffsetCells(100, 100, 10) == 0);
    CHECK(centreOffsetCells(100, 200, 10) == 0);
    CHECK(centreOffsetCells(100, 40, 0) == 0);
}
