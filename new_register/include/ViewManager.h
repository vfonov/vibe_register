#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "AppState.h"

class GraphicsBackend;

class ViewManager {
public:
    ViewManager(AppState& state, GraphicsBackend& backend);

    void updateSliceTexture(int volumeIndex, int viewIndex);
    void updateOverlayTexture(int viewIndex);
    void updateAllOverlayTextures();
    void syncCursors();
    void syncZoom(int sourceVolume, int viewIndex);
    void syncPan(int sourceVolume, int viewIndex);
    void resetViews();

    /// Create/update slice textures for all loaded volumes and overlay.
    /// Skips placeholder volumes (empty data).
    void initializeAllTextures();

    /// Destroy all slice textures and overlay textures.
    void destroyAllTextures();

    static void sliceIndicesToWorld(const Volume& vol, const int indices[3], double world[3]);
    static void worldToSliceIndices(const Volume& vol, const double world[3], int indices[3]);

    /// Invalidate label-to-index cache for a volume (call when colour map changes)
    void invalidateLabelCache(int volumeIndex);

private:
    AppState& state_;
    GraphicsBackend& backend_;

    /// Reusable pixel buffer to avoid per-call allocation.
    std::vector<uint32_t> pixelBuf_;

    /// Cache for label-to-index mapping in label mode with colour map
    /// Key: volume index, Value: map of labelId -> index in colour map
    std::unordered_map<int, std::unordered_map<int, int>> labelToIndexCache_;
    std::unordered_map<int, size_t> labelCacheSize_;
};
