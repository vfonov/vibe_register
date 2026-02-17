#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "AppState.h"

class ViewManager {
public:
    explicit ViewManager(AppState& state);

    void updateSliceTexture(int volumeIndex, int viewIndex);
    void updateOverlayTexture(int viewIndex);
    void updateAllOverlayTextures();
    void syncCursors();
    void resetViews();

    static void sliceIndicesToWorld(const Volume& vol, const int indices[3], double world[3]);
    static void worldToSliceIndices(const Volume& vol, const double world[3], int indices[3]);

private:
    AppState& state_;
};
