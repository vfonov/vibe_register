#pragma once

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "ColourMap.h"
#include "Volume.h"
#include "VulkanHelpers.h"

class AppConfig;

constexpr int kClampCurrent = -2;
constexpr int kClampTransparent = -1;

struct VolumeViewState {
    std::unique_ptr<VulkanTexture> sliceTextures[3];
    glm::ivec3 sliceIndices{0, 0, 0};
    std::array<double, 2> valueRange = {0.0, 1.0};
    glm::dvec3 dragAccum{0.0, 0.0, 0.0};
    ColourMapType colourMap = ColourMapType::GrayScale;

    glm::dvec3 zoom{1.0, 1.0, 1.0};
    glm::dvec3 panU{0.0, 0.0, 0.0};
    glm::dvec3 panV{0.0, 0.0, 0.0};

    float overlayAlpha = 1.0f;

    int underColourMode = kClampCurrent;
    int overColourMode = kClampCurrent;
};

struct OverlayState {
    std::unique_ptr<VulkanTexture> textures[3];
    glm::dvec3 zoom{1.0, 1.0, 1.0};
    glm::dvec3 panU{0.5, 0.5, 0.5};
    glm::dvec3 panV{0.5, 0.5, 0.5};
    glm::dvec3 dragAccum{0.0, 0.0, 0.0};
};

class AppState {
public:
    std::vector<Volume> volumes_;
    std::vector<std::string> volumeNames_;
    std::vector<std::string> volumePaths_;
    std::vector<VolumeViewState> viewStates_;
    OverlayState overlay_;

    bool tagsVisible_ = true;
    bool cleanMode_ = false;
    bool syncCursors_ = false;
    bool syncZoom_ = false;
    bool syncPan_ = false;
    int lastSyncSource_ = 0;
    int lastSyncView_ = 0;
    bool cursorSyncDirty_ = false;
    float dpiScale_ = 1.0f;
    std::string localConfigPath_;
    bool layoutInitialized_ = false;

    int selectedTagIndex_ = -1;
    bool tagListWindowVisible_ = false;

    int volumeCount() const { return static_cast<int>(volumes_.size()); }
    bool hasOverlay() const { return volumes_.size() > 1; }
    int getMaxTagCount() const;
    bool anyVolumeHasTags() const;
    void setSelectedTag(int index);

    Volume& getVolume(int index) { return volumes_[index]; }
    const Volume& getVolume(int index) const { return volumes_[index]; }
    VolumeViewState& getViewState(int index) { return viewStates_[index]; }
    const VolumeViewState& getViewState(int index) const { return viewStates_[index]; }

    void loadVolume(const std::string& path);
    void loadTagsForVolume(int index);
    void initializeViewStates();
    void applyConfig(const AppConfig& cfg, int defaultWindowWidth, int defaultWindowHeight);

    /// Clear all volumes, view states, and overlay textures.
    /// Vulkan resources are released via ~VulkanTexture().
    void clearAllVolumes();

    /// Replace all volumes with those loaded from the given file paths.
    /// Empty paths produce placeholder volumes with name "(missing)".
    /// Failed loads produce placeholder volumes with name "(error)".
    /// Caller must call ViewManager::initializeAllTextures() afterward.
    void loadVolumeSet(const std::vector<std::string>& paths);
};
