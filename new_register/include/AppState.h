#pragma once

#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>

#include <glm/glm.hpp>

#include "ColourMap.h"
#include "Volume.h"
#include "Transform.h"
#include "GraphicsBackend.h"  // for Texture

class AppConfig;

inline std::atomic<bool>& debugLoggingEnabled()
{
    static std::atomic<bool> flag{false};
    return flag;
}

constexpr int kClampCurrent = -2;
constexpr int kClampTransparent = -1;
constexpr int kClampBlack = -3;
constexpr int kClampYellow = -4;
constexpr int kClampWhite = -5;
constexpr int kClampRed = -6;
constexpr int kClampGreen = -7;
constexpr int kClampBlue = -8;

/// Resolve a clamp mode to a packed 0xAABBGGRR colour.
/// @param mode        One of the kClamp* constants or a ColourMapType index.
/// @param currentMap  The colour map currently applied to the volume.
/// @param isOver      true = over colour (top of LUT), false = under (bottom).
/// @param invert      true if the colour map is inverted.
inline uint32_t resolveClampColour(int mode, ColourMapType currentMap,
                                   bool isOver, bool invert = false)
{
    if (mode == kClampTransparent)
        return 0x00000000;
    if (mode == kClampBlack)
        return 0xFF000000;
    if (mode == kClampRed)
        return 0xFF0000FF;   // 0xAABBGGRR: R=FF G=00 B=00 A=FF
    if (mode == kClampGreen)
        return 0xFF00FF00;   // 0xAABBGGRR: R=00 G=FF B=00 A=FF
    if (mode == kClampBlue)
        return 0xFFFF0000;   // 0xAABBGGRR: R=00 G=00 B=FF A=FF
    if (mode == kClampYellow)
        return 0xFF00FFFF;   // 0xAABBGGRR: R=FF G=FF B=00 A=FF
    if (mode == kClampWhite)
        return 0xFFFFFFFF;

    // kClampCurrent or explicit colour-map index
    ColourMapType mapToUse = currentMap;
    if (mode >= 0 && mode < colourMapCount())
        mapToUse = static_cast<ColourMapType>(mode);

    const ColourLut& lut = colourMapLut(mapToUse);
    if (isOver)
        return invert ? lut.table[0] : lut.table[255];
    else
        return invert ? lut.table[255] : lut.table[0];
}

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

    bool useLogTransform = false;
    bool invertColourMap = false;
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
    bool showHotkeysPopup_ = false;
    bool cleanMode_ = false;
    bool syncCursors_ = false;
    bool syncZoom_ = false;
    bool syncPan_ = false;
    bool showCrosshairs_ = true;
    int lastSyncSource_ = 0;
    int lastSyncView_ = 0;
    bool cursorSyncDirty_ = false;
    int activeView_ = 0;  ///< Last-clicked view: 0=Axial, 1=Sagittal, 2=Coronal
    float dpiScale_ = 1.0f;
    std::string localConfigPath_;
    bool layoutInitialized_ = false;

    std::array<float, 3> sharedViewRatios = {0.33f, 0.33f, 0.34f};

    /// Per-view visibility: 0=Axial, 1=Sagittal, 2=Coronal.
    std::array<bool, 3> viewVisible = {true, true, true};

    int selectedTagIndex_ = -1;
    bool tagListWindowVisible_ = false;
    bool autoSaveTags_ = false;
    bool usePerVolumeTagFiles_ = false;  ///< true = per-volume .tag files, false = global/combined

    /// --- Transform state ---
    TransformType transformType_ = TransformType::LSQ6;
    TransformResult transformResult_;
    bool transformOutOfDate_ = true;  ///< Set when tags change
    char xfmFilePath_[256] = "transform.xfm";  ///< User-editable .xfm output path

    /// --- Combined tag file path ---
    /// When non-empty, tags are saved/loaded as a single two-volume .tag file
    /// instead of separate per-volume files.  Set via --tags CLI or UI InputText.
    char combinedTagPath_[512] = "";

    /// --- Font configuration (persisted in config JSON) ---
    char fontPath_[512] = "";   ///< Path to .ttf file, or empty for built-in ProggyForever
    float fontSize_ = 13.0f;   ///< Base font size in pixels at 1.0x scale

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

    /// Resolve duplicate basenames in volumeNames_ by progressively
    /// prepending parent directory components from volumePaths_ until
    /// every entry is unique.  Call after all volumes have been added.
    void disambiguateVolumeNames();

    /// Save both volumes' tags into a single two-volume .tag file at combinedTagPath_.
    /// Requires at least 2 loaded volumes.  Uses volume 0's labels.
    /// @return true on success, false on failure.
    bool saveCombinedTags();

    /// Load a two-volume .tag file and distribute points to volumes 0 and 1.
    /// @param path Path to the .tag file.
    /// @return true if the file was a two-volume file and loaded successfully.
    bool loadCombinedTags(const std::string& path);

    /// Save tags using the appropriate strategy: combined if combinedTagPath_
    /// is set and we have 2+ volumes, otherwise per-volume.
    void saveTags();
    void applyConfig(const AppConfig& cfg, int defaultWindowWidth, int defaultWindowHeight);

    /// Recompute the transform from tag point pairs (vol 0 -> vol 1).
    /// Only recomputes if transformOutOfDate_ is true.
    /// Requires at least 2 volumes with matching tag counts >= kMinPointsLinear.
    /// @return true if the transform was actually recomputed (caller should
    ///         rebuild overlay textures), false if it was already up to date.
    bool recomputeTransform();

    /// Mark transform as needing recomputation (call after any tag change).
    void invalidateTransform() { transformOutOfDate_ = true; }

    /// Set the transform type and recompute.
    void setTransformType(TransformType type);

    /// Get tag point pairs from volumes 0 and 1.
    /// Returns the number of valid pairs found.
    int getTagPairs(std::vector<glm::dvec3>& vol1Tags,
                    std::vector<glm::dvec3>& vol2Tags) const;

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
