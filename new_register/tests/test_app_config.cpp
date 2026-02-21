// test_app_config.cpp — Tests for AppConfig JSON serialization and file I/O.
//
// Verifies loadConfig(), saveConfig(), and to_json/from_json round-trips
// for VolumeConfig, GlobalConfig, QCColumnConfig, and AppConfig.

#include "AppConfig.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

static int failures = 0;

static void check(bool cond, const char* msg, int line)
{
    if (!cond)
    {
        std::cerr << "FAIL (line " << line << "): " << msg << "\n";
        ++failures;
    }
}

#define CHECK(cond, msg) check((cond), (msg), __LINE__)

static bool approxEq(double a, double b, double tol = 1e-9)
{
    return std::fabs(a - b) < tol;
}

/// RAII helper to create a temp file and remove it on destruction.
struct TmpFile
{
    std::string path;

    TmpFile(const std::string& name)
        : path((std::filesystem::temp_directory_path() / name).string())
    {}

    TmpFile(const std::string& name, const std::string& content)
        : path((std::filesystem::temp_directory_path() / name).string())
    {
        std::ofstream ofs(path, std::ios::trunc);
        ofs << content;
    }

    ~TmpFile() { std::filesystem::remove(path); }
};

// ---------------------------------------------------------------------------
// Test 1: Missing file returns default AppConfig
// ---------------------------------------------------------------------------
static void testMissingFileReturnsDefault()
{
    std::cout << "  testMissingFileReturnsDefault...";

    AppConfig cfg = loadConfig("/nonexistent/path/config_that_does_not_exist.json");

    CHECK(cfg.volumes.empty(), "default config should have no volumes");
    CHECK(cfg.global.defaultColourMap == "GrayScale", "default colour map should be GrayScale");
    CHECK(!cfg.global.windowWidth.has_value(), "default windowWidth should be nullopt");
    CHECK(!cfg.global.windowHeight.has_value(), "default windowHeight should be nullopt");
    CHECK(!cfg.global.syncCursors, "default syncCursors should be false");
    CHECK(!cfg.global.syncZoom, "default syncZoom should be false");
    CHECK(!cfg.global.syncPan, "default syncPan should be false");
    CHECK(!cfg.global.tagListVisible, "default tagListVisible should be false");
    CHECK(cfg.global.showOverlay, "default showOverlay should be true");
    CHECK(!cfg.qcColumns.has_value(), "default qcColumns should be nullopt");

    std::cout << " done\n";
}

// ---------------------------------------------------------------------------
// Test 2: Full round-trip with all fields populated
// ---------------------------------------------------------------------------
static void testSaveAndReloadRoundTrip()
{
    std::cout << "  testSaveAndReloadRoundTrip...";
    TmpFile tmp("test_config_rt.json");

    // Build a fully populated config
    AppConfig original;

    original.global.defaultColourMap = "HotMetal";
    original.global.windowWidth = 1920;
    original.global.windowHeight = 1080;
    original.global.syncCursors = true;
    original.global.syncZoom = true;
    original.global.syncPan = true;
    original.global.tagListVisible = true;
    original.global.showOverlay = false;

    VolumeConfig v1;
    v1.path = "/data/vol1.mnc";
    v1.colourMap = "Spectral";
    v1.valueMin = -10.5;
    v1.valueMax = 200.3;
    v1.sliceIndices = {50, 100, 75};
    v1.zoom = {2.0, 1.5, 3.0};
    v1.panU = {0.1, 0.2, 0.3};
    v1.panV = {0.7, 0.8, 0.9};

    VolumeConfig v2;
    v2.path = "/data/vol2.mnc";
    v2.colourMap = "Red";
    v2.valueMin = 0.0;
    v2.valueMax = 100.0;
    v2.sliceIndices = {10, 20, 30};
    v2.zoom = {1.0, 1.0, 1.0};
    v2.panU = {0.5, 0.5, 0.5};
    v2.panV = {0.5, 0.5, 0.5};

    original.volumes = {v1, v2};

    // Save and reload
    saveConfig(original, tmp.path);
    AppConfig loaded = loadConfig(tmp.path);

    // Verify GlobalConfig
    CHECK(loaded.global.defaultColourMap == "HotMetal", "global.defaultColourMap");
    CHECK(loaded.global.windowWidth.has_value() && *loaded.global.windowWidth == 1920,
          "global.windowWidth");
    CHECK(loaded.global.windowHeight.has_value() && *loaded.global.windowHeight == 1080,
          "global.windowHeight");
    CHECK(loaded.global.syncCursors == true, "global.syncCursors");
    CHECK(loaded.global.syncZoom == true, "global.syncZoom");
    CHECK(loaded.global.syncPan == true, "global.syncPan");
    CHECK(loaded.global.tagListVisible == true, "global.tagListVisible");
    CHECK(loaded.global.showOverlay == false, "global.showOverlay");

    // Verify volumes
    CHECK(loaded.volumes.size() == 2, "should have 2 volumes");

    const auto& lv1 = loaded.volumes[0];
    CHECK(lv1.path == "/data/vol1.mnc", "vol1.path");
    CHECK(lv1.colourMap == "Spectral", "vol1.colourMap");
    CHECK(lv1.valueMin.has_value() && approxEq(*lv1.valueMin, -10.5), "vol1.valueMin");
    CHECK(lv1.valueMax.has_value() && approxEq(*lv1.valueMax, 200.3), "vol1.valueMax");
    CHECK(lv1.sliceIndices[0] == 50 && lv1.sliceIndices[1] == 100 && lv1.sliceIndices[2] == 75,
          "vol1.sliceIndices");
    CHECK(approxEq(lv1.zoom[0], 2.0) && approxEq(lv1.zoom[1], 1.5) && approxEq(lv1.zoom[2], 3.0),
          "vol1.zoom");
    CHECK(approxEq(lv1.panU[0], 0.1) && approxEq(lv1.panU[1], 0.2) && approxEq(lv1.panU[2], 0.3),
          "vol1.panU");
    CHECK(approxEq(lv1.panV[0], 0.7) && approxEq(lv1.panV[1], 0.8) && approxEq(lv1.panV[2], 0.9),
          "vol1.panV");

    const auto& lv2 = loaded.volumes[1];
    CHECK(lv2.path == "/data/vol2.mnc", "vol2.path");
    CHECK(lv2.colourMap == "Red", "vol2.colourMap");
    CHECK(lv2.valueMin.has_value() && approxEq(*lv2.valueMin, 0.0), "vol2.valueMin");
    CHECK(lv2.valueMax.has_value() && approxEq(*lv2.valueMax, 100.0), "vol2.valueMax");

    std::cout << " done\n";
}

// ---------------------------------------------------------------------------
// Test 3: Optional fields omitted survive round-trip as nullopt
// ---------------------------------------------------------------------------
static void testOptionalFieldsOmitted()
{
    std::cout << "  testOptionalFieldsOmitted...";
    TmpFile tmp("test_config_opt.json");

    AppConfig original;
    // Leave all optional fields at defaults (nullopt)
    VolumeConfig v;
    v.path = "/data/test.mnc";
    // valueMin, valueMax left as nullopt
    original.volumes = {v};
    // windowWidth, windowHeight left as nullopt
    // qcColumns left as nullopt

    saveConfig(original, tmp.path);
    AppConfig loaded = loadConfig(tmp.path);

    CHECK(!loaded.global.windowWidth.has_value(), "windowWidth should remain nullopt");
    CHECK(!loaded.global.windowHeight.has_value(), "windowHeight should remain nullopt");
    CHECK(loaded.volumes.size() == 1, "should have 1 volume");
    CHECK(!loaded.volumes[0].valueMin.has_value(), "valueMin should remain nullopt");
    CHECK(!loaded.volumes[0].valueMax.has_value(), "valueMax should remain nullopt");
    CHECK(!loaded.qcColumns.has_value(), "qcColumns should remain nullopt");

    std::cout << " done\n";
}

// ---------------------------------------------------------------------------
// Test 4: VolumeConfig with only path set gets correct defaults
// ---------------------------------------------------------------------------
static void testVolumeConfigMinimalFields()
{
    std::cout << "  testVolumeConfigMinimalFields...";
    TmpFile tmp("test_config_min.json");

    AppConfig original;
    VolumeConfig v;
    v.path = "/data/minimal.mnc";
    original.volumes = {v};

    saveConfig(original, tmp.path);
    AppConfig loaded = loadConfig(tmp.path);

    CHECK(loaded.volumes.size() == 1, "should have 1 volume");
    const auto& lv = loaded.volumes[0];
    CHECK(lv.path == "/data/minimal.mnc", "path should survive");
    CHECK(lv.colourMap == "GrayScale", "default colourMap should be GrayScale");
    CHECK(lv.sliceIndices[0] == -1 && lv.sliceIndices[1] == -1 && lv.sliceIndices[2] == -1,
          "default sliceIndices should be {-1,-1,-1}");
    CHECK(approxEq(lv.zoom[0], 1.0) && approxEq(lv.zoom[1], 1.0) && approxEq(lv.zoom[2], 1.0),
          "default zoom should be {1,1,1}");
    CHECK(approxEq(lv.panU[0], 0.5) && approxEq(lv.panU[1], 0.5) && approxEq(lv.panU[2], 0.5),
          "default panU should be {0.5,0.5,0.5}");
    CHECK(approxEq(lv.panV[0], 0.5) && approxEq(lv.panV[1], 0.5) && approxEq(lv.panV[2], 0.5),
          "default panV should be {0.5,0.5,0.5}");

    std::cout << " done\n";
}

// ---------------------------------------------------------------------------
// Test 5: QC columns round-trip
// ---------------------------------------------------------------------------
static void testQCColumnsRoundTrip()
{
    std::cout << "  testQCColumnsRoundTrip...";
    TmpFile tmp("test_config_qc.json");

    AppConfig original;
    std::map<std::string, QCColumnConfig> cols;

    QCColumnConfig t1;
    t1.colourMap = "HotMetal";
    t1.valueMin = 0.0;
    t1.valueMax = 100.0;
    cols["T1"] = t1;

    QCColumnConfig t2;
    t2.colourMap = "Spectral";
    // valueMin/valueMax left as nullopt
    cols["T2"] = t2;

    original.qcColumns = cols;

    saveConfig(original, tmp.path);
    AppConfig loaded = loadConfig(tmp.path);

    CHECK(loaded.qcColumns.has_value(), "qcColumns should be present");
    const auto& lc = *loaded.qcColumns;
    CHECK(lc.size() == 2, "should have 2 QC columns");
    CHECK(lc.count("T1") == 1, "should have T1 column");
    CHECK(lc.count("T2") == 1, "should have T2 column");

    CHECK(lc.at("T1").colourMap == "HotMetal", "T1 colourMap");
    CHECK(lc.at("T1").valueMin.has_value() && approxEq(*lc.at("T1").valueMin, 0.0),
          "T1 valueMin");
    CHECK(lc.at("T1").valueMax.has_value() && approxEq(*lc.at("T1").valueMax, 100.0),
          "T1 valueMax");

    CHECK(lc.at("T2").colourMap == "Spectral", "T2 colourMap");
    CHECK(!lc.at("T2").valueMin.has_value(), "T2 valueMin should be nullopt");
    CHECK(!lc.at("T2").valueMax.has_value(), "T2 valueMax should be nullopt");

    std::cout << " done\n";
}

// ---------------------------------------------------------------------------
// Test 6: QC columns absent
// ---------------------------------------------------------------------------
static void testQCColumnsAbsent()
{
    std::cout << "  testQCColumnsAbsent...";
    TmpFile tmp("test_config_noqc.json");

    AppConfig original;
    // qcColumns left as nullopt

    saveConfig(original, tmp.path);
    AppConfig loaded = loadConfig(tmp.path);

    CHECK(!loaded.qcColumns.has_value(), "qcColumns should remain nullopt when not saved");

    std::cout << " done\n";
}

// ---------------------------------------------------------------------------
// Test 7: Malformed JSON throws
// ---------------------------------------------------------------------------
static void testMalformedJsonThrows()
{
    std::cout << "  testMalformedJsonThrows...";

    TmpFile tmp("test_config_bad.json", "{ this is not valid json !!!");

    bool caught = false;
    try
    {
        loadConfig(tmp.path);
    }
    catch (const std::runtime_error&)
    {
        caught = true;
    }
    CHECK(caught, "loadConfig should throw std::runtime_error on malformed JSON");

    std::cout << " done\n";
}

// ---------------------------------------------------------------------------
// Test 8: Invalid structure throws
// ---------------------------------------------------------------------------
static void testInvalidStructureThrows()
{
    std::cout << "  testInvalidStructureThrows...";

    // Valid JSON but "global" is an integer instead of an object
    TmpFile tmp("test_config_badstruct.json",
                R"({"global": 42, "volumes": "not_an_array"})");

    bool caught = false;
    try
    {
        loadConfig(tmp.path);
    }
    catch (const std::runtime_error&)
    {
        caught = true;
    }
    CHECK(caught, "loadConfig should throw std::runtime_error on invalid structure");

    std::cout << " done\n";
}

// ---------------------------------------------------------------------------
// Test 9: saveConfig creates parent directories
// ---------------------------------------------------------------------------
static void testSaveCreatesParentDir()
{
    std::cout << "  testSaveCreatesParentDir...";

    std::string nested =
        (std::filesystem::temp_directory_path() / "test_nr_cfg_sub" / "deep" / "config.json")
            .string();

    // Clean up from any previous run
    std::filesystem::remove_all(
        std::filesystem::temp_directory_path() / "test_nr_cfg_sub");

    AppConfig cfg;
    cfg.global.defaultColourMap = "Blue";

    saveConfig(cfg, nested);
    CHECK(std::filesystem::exists(nested), "config file should exist in nested dir");

    // Verify it can be loaded back
    AppConfig loaded = loadConfig(nested);
    CHECK(loaded.global.defaultColourMap == "Blue", "nested config should round-trip");

    // Clean up
    std::filesystem::remove_all(
        std::filesystem::temp_directory_path() / "test_nr_cfg_sub");

    std::cout << " done\n";
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main()
{
    std::cout << "=== AppConfig Tests ===\n";

    testMissingFileReturnsDefault();
    testSaveAndReloadRoundTrip();
    testOptionalFieldsOmitted();
    testVolumeConfigMinimalFields();
    testQCColumnsRoundTrip();
    testQCColumnsAbsent();
    testMalformedJsonThrows();
    testInvalidStructureThrows();
    testSaveCreatesParentDir();

    std::cout << "\n";
    if (failures == 0)
    {
        std::cout << "All AppConfig tests PASSED.\n";
        return 0;
    }
    else
    {
        std::cout << failures << " AppConfig test(s) FAILED.\n";
        return 1;
    }
}
