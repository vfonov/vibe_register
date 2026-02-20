#include "AppState.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include "AppConfig.h"

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
        } catch (const std::exception& e) {
            std::cerr << "Exception loading tag file: " << e.what() << "\n";
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

        for (int v = 0; v < 3; ++v) {
            state.zoom[v] = 1.0f;
            state.panU[v] = 0.5f;
            state.panV[v] = 0.5f;
        }
    }
}

void AppState::applyConfig(const AppConfig& cfg, int defaultWindowWidth, int defaultWindowHeight) {
    syncCursors_ = cfg.global.syncCursors;
    syncZoom_ = cfg.global.syncZoom;
    syncPan_ = cfg.global.syncPan;
    tagListWindowVisible_ = cfg.global.tagListVisible;
    showOverlay_ = cfg.global.showOverlay;

    for (int vi = 0; vi < static_cast<int>(volumes_.size()); ++vi) {
        VolumeViewState& state = viewStates_[vi];
        const Volume& vol = volumes_[vi];

        const VolumeConfig* vc = nullptr;
        for (const auto& v : cfg.volumes) {
            if (v.path == volumePaths_[vi]) {
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

            state.zoom[0] = vc->zoom[0];
            state.zoom[1] = vc->zoom[1];
            state.zoom[2] = vc->zoom[2];
            state.panU[0] = vc->panU[0];
            state.panU[1] = vc->panU[1];
            state.panU[2] = vc->panU[2];
            state.panV[0] = vc->panV[0];
            state.panV[1] = vc->panV[1];
            state.panV[2] = vc->panV[2];
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

    initializeViewStates();
}
