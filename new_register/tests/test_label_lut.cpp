/// test_label_lut.cpp — label volumes: description files, rendering, config.
///
/// Covers: the per-label colours of a description file are used by the
/// renderer (single-slice and overlay paths), the ITK-SNAP description format
/// (opacity 0..1, quoted names, out-of-range colours), and applyConfig()
/// honouring is_label_volume / label_description_file.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "AppConfig.h"
#include "AppState.h"
#include "ColourMap.h"
#include "SliceRenderer.h"
#include "Volume.h"

namespace
{

namespace fs = std::filesystem;

/// Fresh per-test scratch directory, removed on destruction.
class TempDir
{
public:
    TempDir()
    {
        std::random_device rd;
        path_ = fs::temp_directory_path() /
                ("nr_label_test_" + std::to_string(rd()) + std::to_string(rd()));
        fs::create_directories(path_);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    std::string write(const std::string& name, const std::string& text) const
    {
        const std::string p = (path_ / name).string();
        std::ofstream(p) << text;
        return p;
    }

private:
    fs::path path_;
};

/// ITK-SNAP label description: IDX R G B A VIS MSH "LABEL", opacity 0..1.
const char* kItkSnapLut =
    "# ITK-SnAP Label Description File\n"
    "    0     0    0    0        0  0  0    \"Clear Label\"\n"
    "    1   255    0    0        1  1  1    \"Left Hippocampus\"\n"
    "    2     0    0  255        1  0  1    \"Hidden\"\n"
    "    3     0  255    0      0.5  1  1    \"Half\"\n";

/// A 4x4x2 label volume filled with a single label id.
Volume uniformLabelVolume(int labelId)
{
    Volume vol;
    vol.dimensions = {4, 4, 2};
    vol.data.assign(4 * 4 * 2, static_cast<float>(labelId));
    vol.min_value = 0.0f;
    vol.max_value = static_cast<float>(labelId);
    vol.setLabelVolume(true);
    return vol;
}

uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
}

/// Label volumes default to Viridis in main.cpp; use the same here.
VolumeRenderParams labelParams()
{
    VolumeRenderParams p;
    p.colourMap = ColourMapType::Viridis;
    return p;
}

}  // namespace

TEST_CASE("ITK-SNAP label description files are parsed", "[label][lut]")
{
    TempDir tmp;
    Volume vol = uniformLabelVolume(1);
    vol.loadLabelDescriptionFile(tmp.write("lut.txt", kItkSnapLut));

    const LabelInfo* l1 = vol.getLabelInfo(1);
    REQUIRE(l1 != nullptr);
    CHECK(l1->r == 255);
    CHECK(l1->g == 0);
    CHECK(l1->b == 0);
    CHECK(l1->a == 255);  // opacity 1 == fully opaque
    CHECK(l1->visible);
    CHECK(l1->name == "Left Hippocampus");

    const LabelInfo* l2 = vol.getLabelInfo(2);
    REQUIRE(l2 != nullptr);
    CHECK_FALSE(l2->visible);
    CHECK(l2->name == "Hidden");

    const LabelInfo* l3 = vol.getLabelInfo(3);
    REQUIRE(l3 != nullptr);
    CHECK(l3->a == 128);  // opacity 0.5
}

TEST_CASE("Label description colours are clamped to 0..255", "[label][lut]")
{
    TempDir tmp;
    Volume vol = uniformLabelVolume(1);
    vol.loadLabelDescriptionFile(tmp.write("lut.txt",
        "1 300 -5 128 255 1 1 \"Bright\"\n"
        "2 10 20 30 128 1 1 Plain\n"));

    const LabelInfo* l1 = vol.getLabelInfo(1);
    REQUIRE(l1 != nullptr);
    CHECK(l1->r == 255);
    CHECK(l1->g == 0);
    CHECK(l1->b == 128);
    CHECK(l1->a == 255);  // opacities above 1 are on the 0..255 scale

    const LabelInfo* l2 = vol.getLabelInfo(2);
    REQUIRE(l2 != nullptr);
    CHECK(l2->a == 128);
    CHECK(l2->name == "Plain");
}

TEST_CASE("renderSlice uses the label description colours", "[label][render]")
{
    TempDir tmp;
    const std::string lut = tmp.write("lut.txt", kItkSnapLut);

    SECTION("visible label gets its RGBA")
    {
        Volume vol = uniformLabelVolume(1);
        vol.loadLabelDescriptionFile(lut);
        RenderedSlice s = renderSlice(vol, labelParams(), 0, 0);
        REQUIRE_FALSE(s.pixels.empty());
        for (uint32_t px : s.pixels)
            REQUIRE(px == rgba(255, 0, 0, 255));
    }
    SECTION("hidden label is transparent")
    {
        Volume vol = uniformLabelVolume(2);
        vol.loadLabelDescriptionFile(lut);
        RenderedSlice s = renderSlice(vol, labelParams(), 0, 0);
        REQUIRE_FALSE(s.pixels.empty());
        for (uint32_t px : s.pixels)
            REQUIRE((px >> 24) == 0);
    }
    SECTION("without a description file the colour map is used")
    {
        Volume vol = uniformLabelVolume(1);
        RenderedSlice s = renderSlice(vol, labelParams(), 0, 0);
        REQUIRE_FALSE(s.pixels.empty());
        // A single label maps to the top of the colour map.
        for (uint32_t px : s.pixels)
            REQUIRE(px == colourMapLut(ColourMapType::Viridis).table[255]);
    }
}

TEST_CASE("renderOverlaySlice uses the label description colours", "[label][render]")
{
    TempDir tmp;
    Volume vol = uniformLabelVolume(1);
    vol.loadLabelDescriptionFile(tmp.write("lut.txt", kItkSnapLut));

    Volume empty = uniformLabelVolume(0);  // background only: transparent

    RenderedSlice s = renderOverlaySlice({&vol, &empty}, {labelParams(), labelParams()}, 0, 0);
    REQUIRE_FALSE(s.pixels.empty());
    for (uint32_t px : s.pixels)
        REQUIRE(px == rgba(255, 0, 0, 255));
}

TEST_CASE("applyConfig applies label settings", "[label][config]")
{
    TempDir tmp;
    const std::string lut = tmp.write("lut.txt", kItkSnapLut);

    Volume vol;
    vol.dimensions = {4, 4, 2};
    vol.data.assign(4 * 4 * 2, 1.0f);

    AppState state;
    state.volumes_.push_back(std::move(vol));
    state.volumePaths_.push_back("labels.mnc");
    state.volumeNames_.push_back("labels.mnc");
    state.initializeViewStates();

    AppConfig cfg;
    VolumeConfig vc;
    vc.path = "labels.mnc";
    vc.isLabelVolume = true;
    vc.labelDescriptionFile = lut;
    cfg.volumes.push_back(vc);

    SECTION("label flag and description file are applied")
    {
        state.applyConfig(cfg, 800, 600);
        CHECK(state.volumes_[0].isLabelVolume());
        const LabelInfo* l1 = state.volumes_[0].getLabelInfo(1);
        REQUIRE(l1 != nullptr);
        CHECK(l1->name == "Left Hippocampus");
    }
    SECTION("a missing description file is only a warning")
    {
        cfg.volumes[0].labelDescriptionFile = tmp.write("unused", "") + ".missing";
        REQUIRE_NOTHROW(state.applyConfig(cfg, 800, 600));
        CHECK(state.volumes_[0].isLabelVolume());
    }
}
