#include "AppState.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

#include "AppConfig.h"
#include "Transform.h"

// --- VolumeCache implementation ---

Volume* VolumeCache::get(const std::string& path) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = map_.find(path);
    if (it == map_.end())
        return nullptr;
    // Move accessed entry to front of LRU list
    lru_.splice(lru_.begin(), lru_, it->second);
    return &it->second->vol;
}

void VolumeCache::put(const std::string& path, Volume vol) {
    std::lock_guard<std::mutex> lk(mutex_);
    // If already cached, update and move to front
    auto it = map_.find(path);
    if (it != map_.end()) {
        it->second->vol = std::move(vol);
        lru_.splice(lru_.begin(), lru_, it->second);
        return;
    }
    // Evict LRU entry if at capacity
    if (map_.size() >= maxEntries_ && !lru_.empty()) {
        auto& back = lru_.back();
        map_.erase(back.path);
        lru_.pop_back();
    }
    lru_.push_front(Entry{path, std::move(vol)});
    map_[path] = lru_.begin();
}

void VolumeCache::clear() {
    std::lock_guard<std::mutex> lk(mutex_);
    map_.clear();
    lru_.clear();
}

// --- AppState implementation ---

void AppState::loadVolume(const std::string& path) {
    Volume vol;
    vol.load(path);
    volumes_.push_back(std::move(vol));
    volumePaths_.push_back(path);
    volumeNames_.push_back(
        std::filesystem::path(path).filename().string());
}

void AppState::disambiguateVolumeNames()
{
    const int n = static_cast<int>(volumeNames_.size());
    if (n < 2)
        return;

    // Find groups of indices that share the same display name.
    std::unordered_map<std::string, std::vector<int>> groups;
    for (int i = 0; i < n; ++i)
        groups[volumeNames_[i]].push_back(i);

    for (auto& [name, indices] : groups)
    {
        if (indices.size() < 2)
            continue;

        // Progressively prepend parent directory components from the
        // full path until every name in this group is unique.
        // Start with depth=1 (one parent dir + filename).
        for (int depth = 1; depth <= 64; ++depth)
        {
            // Build candidate names at this depth.
            std::vector<std::string> candidates(indices.size());
            for (size_t k = 0; k < indices.size(); ++k)
            {
                std::filesystem::path p(volumePaths_[indices[k]]);
                // Collect up to (depth) parent components + filename.
                std::vector<std::string> parts;
                parts.push_back(p.filename().string());
                std::filesystem::path parent = p.parent_path();
                for (int d = 0; d < depth && parent.has_filename(); ++d)
                {
                    parts.push_back(parent.filename().string());
                    parent = parent.parent_path();
                }
                // Assemble in forward order (parent/.../filename).
                std::string result;
                for (auto it = parts.rbegin(); it != parts.rend(); ++it)
                {
                    if (!result.empty())
                        result += '/';
                    result += *it;
                }
                candidates[k] = std::move(result);
            }

            // Check if all candidates in this group are now unique.
            bool allUnique = true;
            for (size_t a = 0; a < candidates.size() && allUnique; ++a)
                for (size_t b = a + 1; b < candidates.size() && allUnique; ++b)
                    if (candidates[a] == candidates[b])
                        allUnique = false;

            if (allUnique)
            {
                for (size_t k = 0; k < indices.size(); ++k)
                    volumeNames_[indices[k]] = std::move(candidates[k]);
                break;
            }
        }
    }
}

void AppState::loadTagsForVolume(int index) {
    if (index < 0 || index >= static_cast<int>(volumes_.size()))
        return;
    if (volumePaths_[index].empty())
        return;

    std::filesystem::path tagPath(volumePaths_[index]);
    tagPath.replace_extension(".tag");
    if (std::filesystem::exists(tagPath)) {
        try {
            volumes_[index].loadTags(tagPath.string());

            // If this is volume 0 and we have 2+ volumes and the file
            // turned out to be a two-volume tag file, distribute vol2
            // points to volume 1.
            if (index == 0 && volumes_.size() >= 2 &&
                volumes_[0].tags.hasTwoVolumes())
            {
                const auto& pts2 = volumes_[0].tags.points2();
                const auto& labels = volumes_[0].tags.labels();
                volumes_[1].tags.setPoints(pts2);
                volumes_[1].tags.setLabels(labels);
                std::cerr << "Loaded two-volume tag file: "
                          << tagPath << " (" << pts2.size() << " tags)\n";
                invalidateTransform();
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception loading tag file: " << e.what() << "\n";
        }
    }
    if (volumes_[index].hasTags()) {
        invalidateTransform();
    }
}

bool AppState::saveCombinedTags() {
    if (volumes_.size() < 2)
        return false;

    std::string path(combinedTagPath_);
    if (path.empty())
        return false;

    try {
        TagWrapper tw;
        tw.setPoints(volumes_[0].getTagPoints());
        tw.setPoints2(volumes_[1].getTagPoints());
        tw.setLabels(volumes_[0].getTagLabels());
        tw.save(path);
        std::cerr << "Saved combined tags to " << path << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to save combined tags: " << e.what() << "\n";
        return false;
    }
}

bool AppState::loadCombinedTags(const std::string& path) {
    if (volumes_.size() < 2)
        return false;

    try {
        TagWrapper tw;
        tw.load(path);

        // Always load volume 1 points
        volumes_[0].tags.setPoints(tw.points());
        volumes_[0].tags.setLabels(tw.labels());

        if (tw.hasTwoVolumes()) {
            // Distribute volume 2 points
            volumes_[1].tags.setPoints(tw.points2());
            volumes_[1].tags.setLabels(tw.labels());
            std::cerr << "Loaded two-volume tag file: "
                      << path << " (" << tw.points().size() << " tags)\n";
        } else {
            // Single-volume file — only first volume gets tags
            std::cerr << "Loaded single-volume tag file: "
                      << path << " (" << tw.points().size() << " tags)\n";
        }

        // Store the path for future saves
        std::snprintf(combinedTagPath_, sizeof(combinedTagPath_),
                      "%s", path.c_str());

        invalidateTransform();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load combined tags: " << e.what() << "\n";
        return false;
    }
}

void AppState::saveTags() {
    // If per-volume mode is explicitly selected, always use per-volume saving
    if (usePerVolumeTagFiles_) {
        for (int vi = 0; vi < static_cast<int>(volumes_.size()); ++vi) {
            if (volumePaths_[vi].empty())
                continue;
            std::filesystem::path tagPath(volumePaths_[vi]);
            tagPath.replace_extension(".tag");
            try {
                volumes_[vi].saveTags(tagPath.string());
                std::cerr << "Saved tags to " << tagPath << "\n";
            } catch (const std::exception& e) {
                std::cerr << "Failed to save tags: " << e.what() << "\n";
            }
        }
        return;
    }

    // If combined tag path is set and we have 2+ volumes, save combined
    if (combinedTagPath_[0] != '\0' && volumes_.size() >= 2) {
        saveCombinedTags();
    } else {
        // Fall back to per-volume saving
        for (int vi = 0; vi < static_cast<int>(volumes_.size()); ++vi) {
            if (volumePaths_[vi].empty())
                continue;
            std::filesystem::path tagPath(volumePaths_[vi]);
            tagPath.replace_extension(".tag");
            try {
                volumes_[vi].saveTags(tagPath.string());
                std::cerr << "Saved tags to " << tagPath << "\n";
            } catch (const std::exception& e) {
                std::cerr << "Failed to save tags: " << e.what() << "\n";
            }
        }
    }
}

void AppState::initializeViewStates() {
    viewStates_.resize(volumes_.size());

    for (int vi = 0; vi < static_cast<int>(volumes_.size()); ++vi) {
        const Volume& vol = volumes_[vi];
        if (vol.data.empty())
            continue;

        VolumeViewState& state = viewStates_[vi];

        state.sliceIndices.x = vol.dimensions.x / 2;
        state.sliceIndices.y = vol.dimensions.y / 2;
        state.sliceIndices.z = vol.dimensions.z / 2;

        state.valueRange[0] = vol.min_value;
        state.valueRange[1] = vol.max_value;
        state.useLogTransform = false;
        state.invertColourMap = false;

        for (int v = 0; v < 3; ++v) {
            state.zoom[v] = 1.0f;
            state.panU[v] = 0.5f;
            state.panV[v] = 0.5f;
        }
    }
}

/// Absolute, normalised form of a path for comparing config entries with
/// loaded volumes; falls back to the path as given if it cannot be resolved.
static std::string normalisedPath(const std::string& path)
{
    if (path.empty())
        return path;
    std::error_code ec;
    auto abs = std::filesystem::absolute(path, ec);
    if (ec)
        return path;
    auto p = std::filesystem::weakly_canonical(abs, ec);
    return ec ? abs.lexically_normal().string() : p.string();
}

void AppState::applyConfig(const AppConfig& cfg, int defaultWindowWidth, int defaultWindowHeight) {
    syncCursors_ = cfg.global.syncCursors;
    syncZoom_ = cfg.global.syncZoom;
    syncPan_ = cfg.global.syncPan;
    tagListWindowVisible_ = cfg.global.tagListVisible;
    showOverlay_ = cfg.global.showOverlay;
    showCrosshairs_ = cfg.global.showCrosshairs;
    autoSaveTags_ = cfg.global.autoSaveTags;
    transformType_ = transformTypeFromString(cfg.global.transformType);
    transformOutOfDate_ = true;
    viewVisible = cfg.global.viewVisible;
    std::snprintf(fontPath_, sizeof(fontPath_), "%s", cfg.global.fontPath.c_str());
    fontSize_ = cfg.global.fontSize;

    for (int vi = 0; vi < static_cast<int>(volumes_.size()); ++vi) {
        VolumeViewState& state = viewStates_[vi];
        const Volume& vol = volumes_[vi];

        // Match config entries to volumes by file, not spelling:
        // "./a.mnc", "a.mnc" and "/abs/a.mnc" name the same volume.
        const VolumeConfig* vc = nullptr;
        const std::string volKey = normalisedPath(volumePaths_[vi]);
        for (const auto& v : cfg.volumes) {
            if (v.path == volumePaths_[vi] || normalisedPath(v.path) == volKey) {
                vc = &v;
                break;
            }
        }

        auto defaultCm = colourMapByName(cfg.global.defaultColourMap);
        if (defaultCm.has_value())
            state.colourMap = defaultCm.value();

        if (vc) {
            auto cm = colourMapByName(vc->colourMap);
            if (cm.has_value())
                state.colourMap = cm.value();

            if (vc->valueMin.has_value())
                state.valueRange[0] = vc->valueMin.value();
            if (vc->valueMax.has_value())
                state.valueRange[1] = vc->valueMax.value();

            if (vc->sliceIndices[0] >= 0)
                state.sliceIndices.x = std::clamp(vc->sliceIndices[0], 0, vol.dimensions.x - 1);
            if (vc->sliceIndices[1] >= 0)
                state.sliceIndices.y = std::clamp(vc->sliceIndices[1], 0, vol.dimensions.y - 1);
            if (vc->sliceIndices[2] >= 0)
                state.sliceIndices.z = std::clamp(vc->sliceIndices[2], 0, vol.dimensions.z - 1);

            // Keep hand-edited values usable: zoom within the UI range,
            // non-finite values back to the defaults.
            for (int v = 0; v < 3; ++v)
            {
                const double z = vc->zoom[v];
                state.zoom[v] = std::isfinite(z) ? std::clamp(z, kMinZoom, kMaxZoom) : 1.0;
                state.panU[v] = std::isfinite(vc->panU[v]) ? vc->panU[v] : 0.5;
                state.panV[v] = std::isfinite(vc->panV[v]) ? vc->panV[v] : 0.5;
            }

            state.useLogTransform = vc->useLogTransform;
            state.invertColourMap = vc->invertColourMap;

            if (vc->isLabelVolume)
                volumes_[vi].setLabelVolume(true);
            // A missing or bad description file only warns (see Volume.cpp).
            if (vc->labelDescriptionFile.has_value())
                volumes_[vi].loadLabelDescriptionFile(*vc->labelDescriptionFile);
        }
    }
}

int AppState::getMaxTagCount() const {
    int maxCount = 0;
    for (const auto& vol : volumes_) {
        if (vol.getTagCount() > maxCount)
            maxCount = vol.getTagCount();
    }
    return maxCount;
}

bool AppState::anyVolumeHasTags() const {
    for (const auto& vol : volumes_) {
        if (vol.hasTags())
            return true;
    }
    return false;
}

void AppState::setSelectedTag(int index) {
    selectedTagIndex_ = index;
    if (index < 0)
        return;

    for (int vi = 0; vi < static_cast<int>(volumes_.size()); ++vi) {
        if (index < volumes_[vi].getTagCount()) {
            glm::dvec3 worldPos = volumes_[vi].getTagPoints()[index];
            glm::ivec3 voxel;
            volumes_[vi].transformWorldToVoxel(worldPos, voxel);
            viewStates_[vi].sliceIndices.x = std::clamp(voxel.x, 0, volumes_[vi].dimensions.x - 1);
            viewStates_[vi].sliceIndices.y = std::clamp(voxel.y, 0, volumes_[vi].dimensions.y - 1);
            viewStates_[vi].sliceIndices.z = std::clamp(voxel.z, 0, volumes_[vi].dimensions.z - 1);
        }
    }
}

void AppState::clearAllVolumes() {
    // Reset overlay textures (destructor handles Vulkan cleanup)
    for (int i = 0; i < 3; ++i)
        overlay_.textures[i].reset();

    // Reset per-volume slice textures
    for (auto& vs : viewStates_)
    {
        for (int i = 0; i < 3; ++i)
            vs.sliceTextures[i].reset();
    }

    volumes_.clear();
    volumePaths_.clear();
    volumeNames_.clear();
    viewStates_.clear();
    selectedTagIndex_ = -1;
}

void AppState::loadVolumeSet(const std::vector<std::string>& paths) {
    clearAllVolumes();

    for (const auto& path : paths)
    {
        if (path.empty())
        {
            // Placeholder for missing/empty path
            volumes_.emplace_back();
            volumePaths_.push_back("");
            volumeNames_.push_back("(missing)");
            continue;
        }

        // Check LRU cache first
        Volume* cached = volumeCache_.get(path);
        if (cached)
        {
            // Copy the cached volume (data vector is shared via copy)
            volumes_.push_back(*cached);
            volumePaths_.push_back(path);
            volumeNames_.push_back(
                std::filesystem::path(path).filename().string());
            continue;
        }

        try
        {
            Volume vol;
            vol.load(path);
            // Store a copy in the cache before moving into the active set
            volumeCache_.put(path, vol);
            volumes_.push_back(std::move(vol));
            volumePaths_.push_back(path);
            volumeNames_.push_back(
                std::filesystem::path(path).filename().string());
        }
        catch (const std::exception& e)
        {
            std::cerr << "Failed to load volume: " << e.what() << "\n";
            // Push placeholder on failure
            volumes_.emplace_back();
            volumePaths_.push_back(path);
            volumeNames_.push_back("(error)");
        }
    }

    disambiguateVolumeNames();
    initializeViewStates();
}

std::vector<ViewDisplaySettings> AppState::captureDisplaySettings() const
{
    std::vector<ViewDisplaySettings> saved;
    for (const auto& vs : viewStates_)
    {
        saved.push_back({vs.colourMap, vs.valueRange, vs.sliceIndices,
                         vs.zoom, vs.panU, vs.panV,
                         vs.underColourMode, vs.overColourMode, vs.overlayAlpha,
                         vs.useLogTransform, vs.invertColourMap});
    }
    return saved;
}

void AppState::restoreDisplaySettings(const std::vector<ViewDisplaySettings>& saved)
{
    for (int ci = 0; ci < volumeCount() && ci < static_cast<int>(saved.size()); ++ci)
    {
        if (volumes_[ci].data.empty())
            continue;
        VolumeViewState& vs = viewStates_[ci];
        vs.colourMap = saved[ci].colourMap;
        vs.valueRange = saved[ci].valueRange;
        const Volume& vol = volumes_[ci];
        vs.sliceIndices.x = std::clamp(saved[ci].sliceIndices.x, 0, vol.dimensions.x - 1);
        vs.sliceIndices.y = std::clamp(saved[ci].sliceIndices.y, 0, vol.dimensions.y - 1);
        vs.sliceIndices.z = std::clamp(saved[ci].sliceIndices.z, 0, vol.dimensions.z - 1);
        vs.zoom = saved[ci].zoom;
        vs.panU = saved[ci].panU;
        vs.panV = saved[ci].panV;
        vs.underColourMode = saved[ci].underColourMode;
        vs.overColourMode = saved[ci].overColourMode;
        vs.overlayAlpha = saved[ci].overlayAlpha;
        vs.useLogTransform = saved[ci].useLogTransform;
        vs.invertColourMap = saved[ci].invertColourMap;
    }
}

// --- Transform computation ---

int AppState::getTagPairs(std::vector<glm::dvec3>& vol1Tags,
                          std::vector<glm::dvec3>& vol2Tags) const
{
    vol1Tags.clear();
    vol2Tags.clear();

    if (volumes_.size() < 2)
        return 0;

    const auto& tags0 = volumes_[0].getTagPoints();
    const auto& tags1 = volumes_[1].getTagPoints();

    // Use the minimum count between the two volumes
    int n = std::min(static_cast<int>(tags0.size()),
                     static_cast<int>(tags1.size()));

    for (int i = 0; i < n; ++i)
    {
        vol1Tags.push_back(tags0[i]);
        vol2Tags.push_back(tags1[i]);
    }

    return n;
}

bool AppState::recomputeTransform()
{
    if (!transformOutOfDate_)
        return false;

    transformOutOfDate_ = false;

    std::vector<glm::dvec3> vol1Tags, vol2Tags;
    int n = getTagPairs(vol1Tags, vol2Tags);

    int minPoints = (transformType_ == TransformType::TPS) ?
                    kMinPointsTPS : kMinPointsLinear;

    if (n < minPoints)
    {
        transformResult_ = TransformResult{};
        transformResult_.type = transformType_;
        return true;
    }

    transformResult_ = computeTransform(vol1Tags, vol2Tags, transformType_);
    return true;
}

void AppState::setTransformType(TransformType type)
{
    if (type != transformType_)
    {
        transformType_ = type;
        transformOutOfDate_ = true;
        recomputeTransform();
    }
}
