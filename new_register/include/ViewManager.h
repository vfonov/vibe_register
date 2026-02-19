#pragma once

#include <cstdint>

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

private:
    AppState& state_;
    GraphicsBackend& backend_;
};
