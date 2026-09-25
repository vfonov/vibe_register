/// test_view_manager.cpp — ViewManager textures, checked headless.
///
/// A fake GraphicsBackend records every pixel buffer ViewManager uploads.
/// The slice and overlay textures must match SliceRenderer (the headless
/// renderer used by new_mincpik) for the same parameters.
///
/// Covers: colour map / invert / log / clamp parity for slices and overlays,
/// label volumes ignoring the log transform, the label colour cache following
/// a replaced volume, and syncCursors() with no sync source.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "AppState.h"
#include "GraphicsBackend.h"
#include "SliceRenderer.h"
#include "ViewManager.h"
#include "Volume.h"

namespace
{

namespace fs = std::filesystem;

/// GraphicsBackend that keeps the last pixels uploaded to each texture.
class FakeBackend : public GraphicsBackend
{
public:
    std::unordered_map<const Texture*, std::vector<uint32_t>> pixels;

    void setWindowHints() override {}
    void initialize(GLFWwindow*) override {}
    void shutdown() override {}
    void waitIdle() override {}
    bool needsSwapchainRebuild() const override { return false; }
    void rebuildSwapchain(int, int) override {}
    void beginFrame() override {}
    void endFrame() override {}
    void initImGui(GLFWwindow*) override {}
    void shutdownImGui() override {}
    void imguiNewFrame() override {}
    void imguiRenderDrawData() override {}
    float contentScale() const override { return 1.0f; }
    void setContentScale(float) override {}
    float imguiScale() const override { return 1.0f; }
    void setFontConfig(const std::string&, float) override {}
    void requestClose() override {}
    void windowSize(int& w, int& h) const override { w = 800; h = 600; }
    std::vector<uint8_t> captureScreenshot(int& w, int& h) override { w = h = 0; return {}; }

    std::unique_ptr<Texture> createTexture(int w, int h, const void* data) override
    {
        auto tex = std::make_unique<Texture>();
        tex->width = w;
        tex->height = h;
        store(tex.get(), data);
        return tex;
    }
    void updateTexture(Texture* tex, const void* data) override { store(tex, data); }
    void destroyTexture(Texture* tex) override { pixels.erase(tex); }
    void shutdownTextureSystem() override {}

    const std::vector<uint32_t>& of(const std::unique_ptr<Texture>& tex) const
    {
        REQUIRE(tex);
        auto it = pixels.find(tex.get());
        REQUIRE(it != pixels.end());
        return it->second;
    }

private:
    void store(const Texture* tex, const void* data)
    {
        const auto* p = static_cast<const uint32_t*>(data);
        pixels[tex].assign(p, p + static_cast<size_t>(tex->width) * tex->height);
    }
};

/// Fresh per-test scratch directory, removed on destruction.
class TempDir
{
public:
    TempDir()
    {
        std::random_device rd;
        path_ = fs::temp_directory_path() /
                ("nr_vm_test_" + std::to_string(rd()) + std::to_string(rd()));
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

constexpr int kNx = 5, kNy = 4, kNz = 3;

/// A 5x4x3 volume, value = f(linear index), world = voxel + offset.
Volume makeVolume(float (*value)(int), const glm::dvec3& offset = {0, 0, 0})
{
    Volume vol;
    vol.dimensions = {kNx, kNy, kNz};
    vol.start = offset;
    vol.data.resize(kNx * kNy * kNz);
    for (int i = 0; i < kNx * kNy * kNz; ++i)
        vol.data[i] = value(i);
    vol.voxelToWorld = glm::translate(glm::dmat4(1.0), offset);
    vol.worldToVoxel = glm::inverse(vol.voxelToWorld);
    vol.min_value = vol.data.front();
    vol.max_value = vol.data.back();
    return vol;
}

float ramp(int i) { return static_cast<float>(i); }
float labelsAB(int i) { return static_cast<float>(1 + i % 2); }       // labels 1, 2
float labelsCDE(int i) { return static_cast<float>(5 + 2 * (i % 3)); }  // labels 5, 7, 9

/// Copy the parameters SliceRenderer takes out of a view state.
VolumeRenderParams paramsOf(const VolumeViewState& st)
{
    VolumeRenderParams p;
    p.valueMin = st.valueRange[0];
    p.valueMax = st.valueRange[1];
    p.colourMap = st.colourMap;
    p.overlayAlpha = st.overlayAlpha;
    p.underColourMode = st.underColourMode;
    p.overColourMode = st.overColourMode;
    p.useLogTransform = st.useLogTransform;
    p.invertColourMap = st.invertColourMap;
    return p;
}

int sliceFor(const VolumeViewState& st, int view)
{
    return view == 0 ? st.sliceIndices.z : (view == 1 ? st.sliceIndices.x : st.sliceIndices.y);
}

struct Style
{
    const char* name;
    ColourMapType map;
    double lo, hi;
    bool invert, log;
    int under, over;
};

const Style kStyles[] = {
    {"gray", ColourMapType::GrayScale, 0, 59, false, false, kClampCurrent, kClampCurrent},
    {"hot inverted", ColourMapType::HotMetal, 0, 59, true, false, kClampCurrent, kClampCurrent},
    {"viridis log", ColourMapType::Viridis, 1, 50, false, true, kClampCurrent, kClampCurrent},
    {"clamped", ColourMapType::GrayScale, 10, 40, false, false, kClampTransparent, kClampRed},
    {"inverted clamped", ColourMapType::Viridis, 10, 40, true, false, kClampCurrent, kClampCurrent},
};

void applyStyle(VolumeViewState& st, const Style& s)
{
    st.colourMap = s.map;
    st.valueRange = {s.lo, s.hi};
    st.invertColourMap = s.invert;
    st.useLogTransform = s.log;
    st.underColourMode = s.under;
    st.overColourMode = s.over;
}

void addVolume(AppState& state, Volume vol, const std::string& name)
{
    state.volumes_.push_back(std::move(vol));
    state.volumePaths_.push_back(name);
    state.volumeNames_.push_back(name);
}

}  // namespace

TEST_CASE("Slice textures match SliceRenderer", "[viewmanager][slice]")
{
    const Style& style = kStyles[GENERATE(0, 1, 2, 3, 4)];
    const int view = GENERATE(0, 1, 2);
    INFO("style " << style.name << ", view " << view);

    FakeBackend backend;
    AppState state;
    addVolume(state, makeVolume(ramp), "a.mnc");
    state.initializeViewStates();
    applyStyle(state.viewStates_[0], style);

    ViewManager vm(state, backend);
    vm.updateSliceTexture(0, view);

    const VolumeViewState& st = state.viewStates_[0];
    RenderedSlice ref = renderSlice(state.volumes_[0], paramsOf(st), view, sliceFor(st, view));
    CHECK(backend.of(st.sliceTextures[view]) == ref.pixels);
}

TEST_CASE("Overlay textures match SliceRenderer", "[viewmanager][overlay]")
{
    const Style& style = kStyles[GENERATE(0, 1, 2, 3, 4)];
    const int view = GENERATE(0, 1, 2);
    INFO("style " << style.name << ", view " << view);

    FakeBackend backend;
    AppState state;
    addVolume(state, makeVolume(ramp), "a.mnc");
    addVolume(state, makeVolume(ramp, {1.0, 0.0, 0.0}), "b.mnc");
    state.initializeViewStates();
    applyStyle(state.viewStates_[0], style);
    state.viewStates_[1].overlayAlpha = 0.5f;

    ViewManager vm(state, backend);
    vm.updateOverlayTexture(view);

    RenderedSlice ref = renderOverlaySlice(
        {&state.volumes_[0], &state.volumes_[1]},
        {paramsOf(state.viewStates_[0]), paramsOf(state.viewStates_[1])},
        view, sliceFor(state.viewStates_[0], view));
    CHECK(backend.of(state.overlay_.textures[view]) == ref.pixels);
}

TEST_CASE("Label volumes ignore the log transform", "[viewmanager][label]")
{
    const int view = GENERATE(0, 1, 2);
    INFO("view " << view);
    TempDir tmp;
    const bool withLut = GENERATE(false, true);
    INFO("description file " << withLut);

    Volume vol = makeVolume(labelsCDE);
    vol.setLabelVolume(true);
    if (withLut)
        vol.loadLabelDescriptionFile(tmp.write("lut.txt",
            "5 255 0 0 1 1 1 \"A\"\n7 0 255 0 1 1 1 \"B\"\n9 0 0 255 1 1 1 \"C\"\n"));

    VolumeRenderParams plain;
    plain.colourMap = ColourMapType::Viridis;
    VolumeRenderParams logged = plain;
    logged.useLogTransform = true;
    const int slice = 1;

    RenderedSlice expected = renderSlice(vol, plain, view, slice);
    CHECK(renderSlice(vol, logged, view, slice).pixels == expected.pixels);

    Volume background = makeVolume(ramp);
    VolumeRenderParams hidden;
    hidden.overlayAlpha = 0.0f;
    CHECK(renderOverlaySlice({&vol, &background}, {logged, hidden}, view, slice).pixels ==
          renderOverlaySlice({&vol, &background}, {plain, hidden}, view, slice).pixels);

    FakeBackend backend;
    AppState state;
    addVolume(state, std::move(vol), "labels.mnc");
    state.initializeViewStates();
    state.viewStates_[0].colourMap = ColourMapType::Viridis;
    state.viewStates_[0].useLogTransform = true;
    state.viewStates_[0].sliceIndices = {slice, slice, slice};

    ViewManager vm(state, backend);
    vm.updateSliceTexture(0, view);
    CHECK(backend.of(state.viewStates_[0].sliceTextures[view]) == expected.pixels);
}

TEST_CASE("Label colours follow a replaced volume", "[viewmanager][label]")
{
    FakeBackend backend;
    AppState state;
    Volume first = makeVolume(labelsAB);
    first.setLabelVolume(true);
    addVolume(state, std::move(first), "labels.mnc");
    state.initializeViewStates();
    state.viewStates_[0].colourMap = ColourMapType::Viridis;

    ViewManager vm(state, backend);
    vm.updateSliceTexture(0, 0);

    // Same slot, different label set (as when a QC row or file is swapped).
    Volume second = makeVolume(labelsCDE);
    second.setLabelVolume(true);
    state.volumes_[0] = std::move(second);
    vm.updateSliceTexture(0, 0);

    const VolumeViewState& st = state.viewStates_[0];
    RenderedSlice ref = renderSlice(state.volumes_[0], paramsOf(st), 0, sliceFor(st, 0));
    CHECK(backend.of(st.sliceTextures[0]) == ref.pixels);
}

TEST_CASE("syncCursors without a sync source does nothing", "[viewmanager][sync]")
{
    FakeBackend backend;
    AppState state;
    addVolume(state, makeVolume(ramp), "a.mnc");
    addVolume(state, makeVolume(ramp, {1.0, 0.0, 0.0}), "b.mnc");
    state.initializeViewStates();
    state.viewStates_[1].sliceIndices = {0, 0, 0};

    state.syncCursors_ = true;
    state.lastSyncSource_ = -1;  // set by overlay pan/zoom

    ViewManager vm(state, backend);
    REQUIRE_NOTHROW(vm.syncCursors());
    CHECK(state.viewStates_[1].sliceIndices == glm::ivec3(0, 0, 0));
}
