/// test_long_paths.cpp — long file paths are kept whole.
///
/// The combined tag path and the font path used to be fixed 512-byte char
/// buffers filled with snprintf, so a longer path was silently truncated:
/// tags would then be auto-saved to a different (truncated) file name, and
/// the font path written back to the config would be wrong.

#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "AppConfig.h"
#include "AppState.h"
#include "Volume.h"

namespace fs = std::filesystem;

namespace
{

void addVolumes(AppState& state, int count)
{
    for (int i = 0; i < count; ++i)
    {
        Volume vol;
        vol.dimensions = {4, 4, 4};
        vol.data.assign(64, 1.0f);
        state.volumes_.push_back(std::move(vol));
        state.volumePaths_.push_back("v" + std::to_string(i) + ".mnc");
        state.volumeNames_.push_back("v" + std::to_string(i) + ".mnc");
    }
    state.initializeViewStates();
}

}  // namespace

TEST_CASE("A long font path from the config is kept whole", "[paths]")
{
    const std::string fontPath = "/fonts/" + std::string(600, 'f') + ".ttf";
    AppState state;
    addVolumes(state, 1);
    AppConfig cfg;
    cfg.global.fontPath = fontPath;
    state.applyConfig(cfg, 800, 600);
    CHECK(state.fontPath_ == fontPath);
}

TEST_CASE("A long combined tag path is kept for saving", "[paths][tags]")
{
    // Three 200-character directory levels: > 512 bytes, < PATH_MAX.
    const fs::path root = fs::temp_directory_path() / "nr_long_paths_test";
    fs::path dir = root;
    for (char c : {'a', 'b', 'c'})
        dir /= std::string(200, c);
    fs::create_directories(dir);
    const fs::path tagPath = dir / "pairs.tag";
    {
        std::ofstream ofs(tagPath);
        ofs << "MNI Tag Point File\nVolumes = 2;\n\nPoints =\n"
               " 1 2 3 4 5 6 \"P1\"\n"
               " 7 8 9 10 11 12 \"P2\";\n";
    }
    REQUIRE(tagPath.string().size() > 600);

    AppState state;
    addVolumes(state, 2);
    const bool loaded = state.loadCombinedTags(tagPath.string());
    const std::string kept = state.combinedTagPath_;
    fs::remove_all(root);

    REQUIRE(loaded);
    CHECK(kept == tagPath.string());
}
