#pragma once

#include <string>
#include <vector>

#include <imgui.h>

#include "AppState.h"
#include "GraphicsBackend.h"

class ViewManager;
class QCState;
class Prefetcher;

class Interface {
public:
    Interface(AppState& state, ViewManager& viewManager, QCState& qcState);

    void render(GraphicsBackend& backend, GLFWwindow* window);
    void saveScreenshot(GraphicsBackend& backend);

    /// Set the prefetcher instance (optional, only used in QC mode).
    void setPrefetcher(Prefetcher* prefetcher) { prefetcher_ = prefetcher; }

    static uint32_t resolveClampColour(int mode, ColourMapType currentMap, bool isOver);
    static const char* clampColourLabel(int mode);

private:
    AppState& state_;
    ViewManager& viewManager_;
    QCState& qcState_;
    Prefetcher* prefetcher_ = nullptr;

    std::vector<std::string> columnNames_;
    bool scrollToCurrentRow_ = true;
    bool autosave_ = true;
    ImVec2 lastViewportSize_{0.0f, 0.0f};
    GLFWwindow* interfaceWindow_ = nullptr;

    bool tagFileDialogOpen_ = false;
    bool tagFileDialogIsSave_ = false;
    std::string tagFileDialogPath_;
    std::vector<std::string> tagFileDialogEntries_;
    std::string tagFileDialogCurrentPath_;
    std::string tagFileDialogFilename_;

    bool configFileDialogOpen_ = false;
    bool configFileDialogIsSave_ = false;
    std::string configFileDialogPath_;
    std::vector<std::string> configFileDialogEntries_;
    std::string configFileDialogCurrentPath_;
    std::string configFileDialogFilename_;

    void renderTagFileDialog();
    void updateTagFileDialogEntries();
    void renderConfigFileDialog();
    void updateConfigFileDialogEntries();

    void setupLayout(int numVolumes);
    void renderToolsPanel(GraphicsBackend& backend, GLFWwindow* window);
    int renderVolumeColumn(int vi);
    void renderOverlayPanel();
    void renderTagListWindow();
    void renderQCVerdictPanel(int volumeIndex);
    void switchQCRow(int newRow, GraphicsBackend& backend);
    int renderSliceView(int vi, int viewIndex, const ImVec2& childSize);
    int renderOverlayView(int viewIndex, const ImVec2& childSize);
    bool drawTagsOnSlice(int viewIndex, const ImVec2& imgPos,
                         const ImVec2& imgSize, const ImVec2& uv0, const ImVec2& uv1,
                         const Volume& vol, const glm::ivec3& currentSlice,
                         int selectedTagIndex);
};
