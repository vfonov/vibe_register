/// test_screenshot.cpp — the `s` screenshot key's file-writing half.
///
/// nextScreenshotFilename()'s numbering and saveScreenshot()'s PNG writing
/// are both exercised against an isolated scratch directory (not the repo
/// tree or the ctest working directory) so parallel test runs and repeated
/// runs never collide with leftover screenshotNNNNNN.png files.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "render/Screenshot.hpp"

using namespace mriv::term;
namespace fs = std::filesystem;

namespace
{

/// A fresh, empty directory for one test, removed on destruction. Named
/// with the pid so concurrent ctest workers running this binary do not
/// share a directory.
struct ScratchDir
{
    fs::path path;

    ScratchDir()
        : path(fs::temp_directory_path()
              / ("mriv_test_screenshot_" + std::to_string(::getpid())
                  + "_" + std::to_string(++counter_)))
    {
        fs::remove_all(path);
        fs::create_directories(path);
    }

    ~ScratchDir()
    {
        fs::remove_all(path);
    }

    static inline int counter_ = 0;
};

std::vector<std::uint32_t> makeSolidImage(int w, int h, std::uint32_t colour)
{
    return std::vector<std::uint32_t>(static_cast<std::size_t>(w) * h, colour);
}

void touch(const fs::path& path)
{
    std::ofstream(path) << "x";
}

void testFirstFilenameIsIndexOne()
{
    ScratchDir dir;
    assert(nextScreenshotFilename(dir.path.string())
          == (dir.path / "screenshot000001.png").string());
}

void testNumberingSkipsExistingFiles()
{
    ScratchDir dir;
    touch(dir.path / "screenshot000001.png");
    touch(dir.path / "screenshot000002.png");
    assert(nextScreenshotFilename(dir.path.string())
          == (dir.path / "screenshot000003.png").string());
}

/// A gap must not be reused -- new_register's own convention (Interface.cpp)
/// always continues from the highest existing index, it does not fill holes.
void testNumberingDoesNotFillGaps()
{
    ScratchDir dir;
    touch(dir.path / "screenshot000001.png");
    touch(dir.path / "screenshot000003.png");
    assert(nextScreenshotFilename(dir.path.string())
          == (dir.path / "screenshot000002.png").string());
}

void testSaveScreenshotWritesAPngAtTheExpectedPath()
{
    ScratchDir dir;
    auto pixels = makeSolidImage(4, 3, 0xff0000ffu);

    std::string path = saveScreenshot(pixels.data(), 4, 3, dir.path.string());

    assert(path == (dir.path / "screenshot000001.png").string());
    assert(fs::exists(path));
    assert(fs::file_size(path) > 0);
}

void testSaveScreenshotIncrementsOnEachCall()
{
    ScratchDir dir;
    auto pixels = makeSolidImage(2, 2, 0x11223344u);

    std::string first  = saveScreenshot(pixels.data(), 2, 2, dir.path.string());
    std::string second = saveScreenshot(pixels.data(), 2, 2, dir.path.string());

    assert(first == (dir.path / "screenshot000001.png").string());
    assert(second == (dir.path / "screenshot000002.png").string());
}

void testSaveScreenshotFailsOnAnEmptyFrame()
{
    ScratchDir dir;
    assert(saveScreenshot(nullptr, 0, 0, dir.path.string()).empty());
    assert(saveScreenshot(nullptr, 4, 3, dir.path.string()).empty());
}

} // namespace

int main()
{
    testFirstFilenameIsIndexOne();
    testNumberingSkipsExistingFiles();
    testNumberingDoesNotFillGaps();
    testSaveScreenshotWritesAPngAtTheExpectedPath();
    testSaveScreenshotIncrementsOnEachCall();
    testSaveScreenshotFailsOnAnEmptyFrame();
    return 0;
}
