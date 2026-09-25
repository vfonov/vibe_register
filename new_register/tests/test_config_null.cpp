/// test_config_null.cpp — loadConfig() with explicit JSON nulls.
///
/// Optional config fields (value range, window size, label description
/// file) are written only when set, but a hand-edited or tool-generated
/// config may spell "unset" as null. That must load as unset instead of
/// rejecting the whole file as an invalid structure.

#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "AppConfig.h"

namespace fs = std::filesystem;

TEST_CASE("Null optional fields load as unset", "[config][json]")
{
    const fs::path path = fs::temp_directory_path() / "nr_config_null_test.json";
    {
        std::ofstream ofs(path);
        ofs << R"({
            "global": { "window_width": null, "window_height": null },
            "volumes": [ { "path": "a.mnc", "value_min": null, "value_max": 5.0,
                           "label_description_file": null } ],
            "qc_columns": { "T1": { "colour_map": "GrayScale",
                                    "value_min": null, "value_max": null } }
        })";
    }

    AppConfig cfg;
    REQUIRE_NOTHROW(cfg = loadConfig(path.string()));
    fs::remove(path);

    CHECK_FALSE(cfg.global.windowWidth);
    CHECK_FALSE(cfg.global.windowHeight);
    REQUIRE(cfg.volumes.size() == 1);
    CHECK_FALSE(cfg.volumes[0].valueMin);
    CHECK(cfg.volumes[0].valueMax == std::optional<double>(5.0));
    CHECK_FALSE(cfg.volumes[0].labelDescriptionFile);
    REQUIRE(cfg.qcColumns);
    CHECK_FALSE(cfg.qcColumns->at("T1").valueMin);
    CHECK_FALSE(cfg.qcColumns->at("T1").valueMax);
}
