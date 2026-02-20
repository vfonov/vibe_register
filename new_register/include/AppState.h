#pragma once

#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "ColourMap.h"
#include "Volume.h"
#include "GraphicsBackend.h"  // for Texture

class AppConfig;

constexpr int kClampCurrent = -2;
constexpr int kClampTransparent = -1;

struct VolumeViewState {
    std::unique_ptr<Texture> sliceTextures[3];
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
    std::unique_ptr<Texture> textures[3];
    glm::dvec3 zoom{1.0, 1.0, 1.0};
    glm::dvec3 panU{0.5, 0.5, 0.5};
    glm::dvec3 panV{0.5, 0.5, 0.5};
    glm::dvec3 dragAccum{0.0, 0.0, 0.0};
};

/// LRU cache for loaded Volume objects.  Keyed by absolute file path.
/// Avoids re-reading MINC files from disk during QC row switches.
class VolumeCache {
public:
    explicit VolumeCache(size_t maxEntries = 8) : maxEntries_(maxEntries) {}

    /// Try to retrieve a cached volume.  On hit, moves the entry to the
    /// front of the LRU list and returns a copy.  On miss, returns nullptr
    /// wrapped in std::optional (empty optional).
    /// Thread-safe: acquires internal mutex.
    Volume* get(const std::string& path);

    /// Insert a volume into the cache, evicting the least-recently-used
    /// entry if capacity is exceeded.  The Volume is moved in.
    /// Thread-safe: acquires internal mutex.
    void put(const std::string& path, Volume vol);

    /// Clear all cached volumes and free memory.
    /// Thread-safe: acquires internal mutex.
    void clear();

    size_t size() const { std::lock_guard<std::mutex> lk(mutex_); return map_.size(); }
    size_t capacity() const { return maxEntries_; }

    /// Expose mutex for external callers that need to hold the lock
    /// across a get()+copy sequence.
    std::mutex& mutex() { return mutex_; }

private:
    struct Entry {
        std::string path;
        Volume vol;
    };
    size_t maxEntries_;
    mutable std::mutex mutex_;
    std::list<Entry> lru_;                           // front = most recent
    std::unordered_map<std::string,
                       std::list<Entry>::iterator> map_;
};

class AppState {
public:
    std::vector<Volume> volumes_;
    std::vector<std::string> volumeNames_;
    std::vector<std::string> volumePaths_;
    std::vector<VolumeViewState> viewStates_;
    OverlayState overlay_;

    bool tagsVisible_ = true;
    bool showOverlay_ = true;
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

    /// LRU volume cache for QC mode row switches.
    VolumeCache volumeCache_;

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
    /// GPU resources are released via Texture destructor.
    void clearAllVolumes();

    /// Replace all volumes with those loaded from the given file paths.
    /// Empty paths produce placeholder volumes with name "(missing)".
    /// Failed loads produce placeholder volumes with name "(error)".
    /// Caller must call ViewManager::initializeAllTextures() afterward.
    /// Uses volumeCache_ to avoid re-reading previously loaded files.
    void loadVolumeSet(const std::vector<std::string>& paths);
};
