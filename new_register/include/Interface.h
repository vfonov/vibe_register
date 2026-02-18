#pragma once

#include <string>
#include <vector>

#include <imgui.h>

#include "AppState.h"
#include "GraphicsBackend.h"

class ViewManager;

class Interface {
public:
    Interface(AppState& state, ViewManager& viewManager);

    void render(GraphicsBackend& backend, GLFWwindow* window);
    void saveScreenshot(GraphicsBackend& backend);

    static uint32_t resolveClampColour(int mode, ColourMapType currentMap, bool isOver);
    static const char* clampColourLabel(int mode);

private:
    AppState& state_;
    ViewManager& viewManager_;

    std::vector<std::string> columnNames_;

    void setupLayout(int numVolumes);
    void renderToolsPanel(GraphicsBackend& backend, GLFWwindow* window);
    void renderVolumeColumn(int vi);
    void renderOverlayPanel();
    void renderTagListWindow();
    int renderSliceView(int vi, int viewIndex, const ImVec2& childSize);
    int renderOverlayView(int viewIndex, const ImVec2& childSize);
    bool drawTagsOnSlice(int viewIndex, const ImVec2& imgPos,
                         const ImVec2& imgSize, const ImVec2& uv0, const ImVec2& uv1,
                         const Volume& vol, const glm::ivec3& currentSlice);
};
