#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "OsPrefetch.h"

#include <imgui.h>
#include <imgui_internal.h>

#include "Interface.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <vector>

#include <GLFW/glfw3.h>

#include "AppConfig.h"
#include "ColourMap.h"
#include "GraphicsBackend.h"
#include "Prefetcher.h"
#include "QCState.h"
#include "Transform.h"
#include "ViewManager.h"

// ---------------------------------------------------------------------------
// Icon texture helpers
// ---------------------------------------------------------------------------

namespace
{

/// Generate pixel data for the "Transparent" icon (box with cross line).
std::vector<uint8_t> generateTransparentIcon(int size)
{
    std::vector<uint8_t> pixels(size * size * 4);
    float half = size / 2.0f;
    
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            int idx = (y * size + x) * 4;
            float nx = (x - half) / half;
            float ny = (y - half) / half;
            
            float distEdge = std::max(std::abs(nx), std::abs(ny));
            bool onBorder = distEdge > 0.85f && distEdge < 0.95f;
            
            float distDiag = std::abs(nx - ny);
            bool onDiag = distDiag < 0.08f;
            
            uint8_t r = 200, g = 200, b = 200, a = 0;
            
            if (onBorder)
            {
                r = g = b = 180;
                a = 255;
            }
            else if (onDiag)
            {
                r = g = b = 150;
                a = 200;
            }
            else
            {
                bool checker = ((x / 4) + (y / 4)) % 2 == 0;
                r = g = b = checker ? 240 : 200;
                a = 255;
            }
            
            pixels[idx + 0] = r;
            pixels[idx + 1] = g;
            pixels[idx + 2] = b;
            pixels[idx + 3] = a;
        }
    }
    return pixels;
}

/// Generate pixel data for the "Current" icon (box with circle).
std::vector<uint8_t> generateCurrentIcon(int size)
{
    std::vector<uint8_t> pixels(size * size * 4);
    float half = size / 2.0f;
    float radius = size * 0.35f;
    
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            int idx = (y * size + x) * 4;
            float dx = x - half;
            float dy = y - half;
            float dist = std::sqrt(dx * dx + dy * dy);
            
            float distEdge = std::max(std::abs(dx / half), std::abs(dy / half));
            bool onBorder = distEdge > 0.85f && distEdge < 0.95f;
            
            bool onCircle = std::abs(dist - radius) < 3.0f;
            
            uint8_t r = 200, g = 200, b = 200, a = 0;
            
            if (onBorder)
            {
                r = g = b = 180;
                a = 255;
            }
            else if (onCircle)
            {
                r = g = b = 100;
                a = 255;
            }
            else
            {
                bool checker = ((x / 4) + (y / 4)) % 2 == 0;
                r = g = b = checker ? 240 : 200;
                a = 255;
            }
            
            pixels[idx + 0] = r;
            pixels[idx + 1] = g;
            pixels[idx + 2] = b;
            pixels[idx + 3] = a;
        }
    }
    return pixels;
}

} // anonymous namespace

Interface::Interface(AppState& state, ViewManager& viewManager, QCState& qcState)
    : state_(state), viewManager_(viewManager), qcState_(qcState) {}

Interface::~Interface() = default;

void Interface::render(GraphicsBackend& backend, GLFWwindow* window) {
    interfaceWindow_ = window;
    
    // Initialize icon textures if not already done
    if (!transparentIcon_ || transparentIcon_->id == 0)
    {
        std::vector<uint8_t> transparentPixels = generateTransparentIcon(32);
        transparentIcon_ = backend.createTexture(32, 32, transparentPixels.data());
    }
    if (!currentIcon_ || currentIcon_->id == 0)
    {
        std::vector<uint8_t> currentPixels = generateCurrentIcon(32);
        currentIcon_ = backend.createTexture(32, 32, currentPixels.data());
    }
    
    int numVolumes = state_.volumeCount();
    bool hasOverlay = state_.hasOverlay();

    if (columnNames_.empty() || static_cast<int>(columnNames_.size()) != numVolumes) {
        columnNames_.clear();
        if (qcState_.active) {
            for (int ci = 0; ci < qcState_.columnCount(); ++ci)
                columnNames_.push_back(qcState_.columnNames[ci]);
        } else {
            for (int vi = 0; vi < numVolumes; ++vi)
                columnNames_.push_back(state_.volumeNames_[vi]);
        }
    }

    ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
        ImGuiDockNodeFlags_AutoHideTabBar);

    // Rebuild dock layout when volumes change OR when the viewport is resized.
    // Without resize detection, only the rightmost "remaining" node (Overlay)
    // absorbs size changes and volume columns stay at their initial pixel sizes.
    ImVec2 vpSize = ImGui::GetMainViewport()->Size;
    bool vpChanged = (fabsf(vpSize.x - lastViewportSize_.x) > 0.5f ||
                      fabsf(vpSize.y - lastViewportSize_.y) > 0.5f);

    if ((!state_.layoutInitialized_ || vpChanged) && numVolumes > 0) {
        state_.layoutInitialized_ = true;
        lastViewportSize_ = vpSize;

        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, vpSize);

        ImGuiID toolsId, contentId;

        // Adapt left-panel width to the number of content columns:
        // fewer volumes → wider tools panel so controls aren't cramped.
        bool showOverlayPanel = hasOverlay;
        if (qcState_.active)
            showOverlayPanel = showOverlayPanel && qcState_.showOverlay;
        else
            showOverlayPanel = showOverlayPanel && state_.showOverlay_;
        int totalColumns = numVolumes + (showOverlayPanel ? 1 : 0);

        float toolsFraction;
        if (totalColumns <= 1)
            toolsFraction = 0.30f;
        else if (totalColumns == 2)
            toolsFraction = 0.20f;
        else if (totalColumns == 3)
            toolsFraction = 0.16f;
        else
            toolsFraction = 0.13f;

        if (qcState_.active) {
            // QC mode: slightly wider for the embedded QC list
            toolsFraction += 0.02f;
            ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, toolsFraction,
                &toolsId, &contentId);
            // Tools takes entire column — QC list is embedded as a fill-remaining child.
            ImGui::DockBuilderDockWindow("Tools", toolsId);
        } else {
            ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, toolsFraction, &toolsId, &contentId);

            // Split the left column into a fixed-height Tools node (top) and a
            // stretchy Tags node (bottom).  We set the Tools node to an absolute
            // pixel height so that on every rebuild (including resize) the Tags
            // panel always gets all remaining space, regardless of what imgui.ini
            // may have saved for these nodes.
            //
            // Ideal Tools height: count the fixed UI rows:
            //   Overlay/Sync checkboxes (~5), view checkboxes (3), Tags label (1),
            //   Save/Load Config (2), Reset/Screenshot/Clean/Hotkeys/Quit (5),
            //   Separators/Spacing (~4) → ~20 rows × (lineHeight + spacing)
            float lineH   = ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y;
            float padding = ImGui::GetStyle().WindowPadding.y * 2.0f;
            float idealToolsH = lineH * 22.0f + padding;        // generous fixed height
            float minTagsH    = lineH * 6.0f  + padding;        // minimum for tag table

            // Clamp so Tags always has at least minTagsH pixels
            float availH = vpSize.y;
            if (idealToolsH > availH - minTagsH)
                idealToolsH = availH - minTagsH;
            if (idealToolsH < lineH * 8.0f)
                idealToolsH = lineH * 8.0f;   // never let Tools disappear entirely

            float toolsRatio = idealToolsH / availH;

            ImGuiID toolsTopId, tagsId;
            ImGui::DockBuilderSplitNode(toolsId, ImGuiDir_Up, toolsRatio, &toolsTopId, &tagsId);

            // Override the saved pixel size so it is anchored to idealToolsH
            // even when imgui.ini contains a stale value from a previous session.
            ImGui::DockBuilderSetNodeSize(toolsTopId, ImVec2(vpSize.x * toolsFraction, idealToolsH));

            ImGui::DockBuilderDockWindow("Tools", toolsTopId);
            ImGui::DockBuilderDockWindow("Tags",  tagsId);
        }

        std::vector<ImGuiID> columnIds(totalColumns);
        if (totalColumns == 1) {
            columnIds[0] = contentId;
        } else {
            ImGuiID remaining = contentId;
            for (int ci = 0; ci < totalColumns - 1; ++ci) {
                float fraction = 1.0f / static_cast<float>(totalColumns - ci);
                ImGuiID leftId, rightId;
                ImGui::DockBuilderSplitNode(remaining, ImGuiDir_Left, fraction, &leftId, &rightId);
                columnIds[ci] = leftId;
                remaining = rightId;
            }
            columnIds[totalColumns - 1] = remaining;
        }

        for (int vi = 0; vi < numVolumes; ++vi) {
            ImGui::DockBuilderDockWindow(columnNames_[vi].c_str(), columnIds[vi]);
        }
        if (showOverlayPanel) {
            ImGui::DockBuilderDockWindow("Overlay", columnIds[totalColumns - 1]);
        }

        ImGui::DockBuilderFinish(dockspaceId);
    }

    if (!state_.cleanMode_) {
        renderToolsPanel(backend, window);
    }

    // Recompute the tag-based transform before any overlay textures are built.
    // This must happen before renderVolumeColumn / renderOverlayPanel so that
    // overlay textures always use the current transform.
    if (state_.volumeCount() >= 2)
    {
        bool transformChanged = state_.recomputeTransform();
        if (transformChanged && state_.hasOverlay())
            viewManager_.updateAllOverlayTextures();
    }

    if (!ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            viewManager_.resetViews();
            if (hasOverlay)
                viewManager_.updateAllOverlayTextures();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) {
            glfwSetWindowShouldClose(window, true);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_C)) {
            state_.cleanMode_ = !state_.cleanMode_;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_P)) {
            saveScreenshot(backend);
        }

        if (qcState_.active) {
            if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, false))
                switchQCRow(qcState_.currentRowIndex + 1, backend);
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false))
                switchQCRow(qcState_.currentRowIndex - 1, backend);
        }

        // +/- keys: step through slices of the last-clicked view.
        // activeView_: 0=Axial (Z), 1=Sagittal (X), 2=Coronal (Y).
        if (numVolumes > 0) {
            bool plus  = ImGui::IsKeyPressed(ImGuiKey_Equal) ||
                         ImGui::IsKeyPressed(ImGuiKey_KeypadAdd);
            bool minus = ImGui::IsKeyPressed(ImGuiKey_Minus) ||
                         ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract);
            if (plus || minus) {
                VolumeViewState& vs = state_.viewStates_[0];
                const Volume& vol = state_.volumes_[0];
                int av = state_.activeView_;
                int* slicePtr = nullptr;
                int maxSlice = 0;
                if (av == 0) {          // Axial -> Z
                    slicePtr = &vs.sliceIndices.z;
                    maxSlice = vol.dimensions.z;
                } else if (av == 1) {   // Sagittal -> X
                    slicePtr = &vs.sliceIndices.x;
                    maxSlice = vol.dimensions.x;
                } else {                // Coronal -> Y
                    slicePtr = &vs.sliceIndices.y;
                    maxSlice = vol.dimensions.y;
                }
                int newVal = *slicePtr + (plus ? 1 : -1);
                newVal = std::clamp(newVal, 0, maxSlice - 1);
                if (newVal != *slicePtr) {
                    *slicePtr = newVal;
                    viewManager_.updateSliceTexture(0, av);
                    if (state_.syncCursors_) {
                        state_.lastSyncSource_ = 0;
                        state_.lastSyncView_ = av;
                        state_.cursorSyncDirty_ = true;
                    }
                    if (state_.hasOverlay())
                        viewManager_.updateOverlayTexture(av);
                }
            }
        }
    }

    // Toggle hotkeys popup with '?' or 'H' - works even when popup is open
    if (ImGui::IsKeyPressed(ImGuiKey_Slash) || ImGui::IsKeyPressed(ImGuiKey_H)) {
        state_.showHotkeysPopup_ = !state_.showHotkeysPopup_;
    }

    int overlayDirtyMask = 0;
    for (int vi = 0; vi < numVolumes; ++vi) {
        overlayDirtyMask |= renderVolumeColumn(vi);
    }
    if (state_.hasOverlay()) {
        for (int v = 0; v < 3; ++v) {
            if (overlayDirtyMask & (1 << v))
                viewManager_.updateOverlayTexture(v);
        }
    }

    if (hasOverlay) {
        bool showOverlayPanel = true;
        if (qcState_.active)
            showOverlayPanel = qcState_.showOverlay;
        else
            showOverlayPanel = state_.showOverlay_;
        if (showOverlayPanel)
            renderOverlayPanel();
    }

    // Tags is a separate dock window in the left column below Tools.
    if (!qcState_.active && !state_.cleanMode_ && state_.volumeCount() > 0) {
        state_.tagListWindowVisible_ = true;
        renderTagListWindow();
    }

    renderTagFileDialog();
    renderConfigFileDialog();
    renderHotkeyPopup();

    if (state_.syncCursors_ && state_.cursorSyncDirty_) {
        viewManager_.syncCursors();
        state_.cursorSyncDirty_ = false;
    }
}

void Interface::saveScreenshot(GraphicsBackend& backend) {
    int width = 0, height = 0;
    auto pixels = backend.captureScreenshot(width, height);
    if (pixels.empty() || width <= 0 || height <= 0) {
        std::cerr << "Screenshot: failed to capture framebuffer\n";
        return;
    }

    int index = 1;
    std::string filename;
    while (true) {
        char fmtBuf[64];
        std::snprintf(fmtBuf, sizeof(fmtBuf), "screenshot%06d.png", index);
        filename = fmtBuf;
        if (!std::filesystem::exists(filename))
            break;
        ++index;
    }

    int ok = stbi_write_png(filename.c_str(), width, height, 4, pixels.data(), width * 4);
    if (!ok) {
        std::cerr << "Screenshot: failed to write " << filename << "\n";
        return;
    }

    std::cout << "Screenshot saved: " << filename << "\n";
}

uint32_t Interface::resolveClampColour(int mode, ColourMapType currentMap, bool isOver) {
    return ::resolveClampColour(mode, currentMap, isOver, false);
}

const char* Interface::clampColourLabel(int mode) {
    if (mode == kClampCurrent)
        return "Current";
    if (mode == kClampTransparent)
        return "Transparent";
    if (mode == kClampBlack)
        return "Black";
    if (mode == kClampYellow)
        return "Yellow";
    if (mode == kClampWhite)
        return "White";
    if (mode >= 0 && mode < colourMapCount())
        return colourMapName(static_cast<ColourMapType>(mode)).data();
    return "Unknown";
}

void Interface::renderToolsPanel(GraphicsBackend& backend, GLFWwindow* window) {
    ImGui::Begin("Tools");
    {
        float btnWidth = ImGui::GetContentRegionAvail().x;
        int numVolumes = state_.volumeCount();
        bool hasOverlay = state_.hasOverlay();

        if (qcState_.active) {
            ImGui::Text("QC Mode");
            ImGui::Text("%d / %d rated", qcState_.ratedCount(), qcState_.rowCount());
            if (qcState_.currentRowIndex >= 0)
                ImGui::Text("ID: %s", qcState_.rowIds[qcState_.currentRowIndex].c_str());
            if (hasOverlay) {
                if (ImGui::Checkbox("Overlay", &qcState_.showOverlay))
                    state_.layoutInitialized_ = false;
            }
            ImGui::Separator();
        }

        if (!qcState_.active && hasOverlay) {
            if (ImGui::Checkbox("Overlay", &state_.showOverlay_))
                state_.layoutInitialized_ = false;
        }

        if (ImGui::Checkbox("Sync Cursor", &state_.syncCursors_)) {
            if (state_.syncCursors_ && numVolumes > 1) {
                state_.lastSyncSource_ = 0;
                state_.lastSyncView_ = 0;
                state_.cursorSyncDirty_ = true;

                glm::dvec3 worldPos;
                state_.volumes_[0].transformVoxelToWorld(state_.viewStates_[0].sliceIndices, worldPos);

                for (int vi = 1; vi < numVolumes; ++vi) {
                    glm::ivec3 newVoxel;
                    state_.volumes_[vi].transformWorldToVoxel(worldPos, newVoxel);
                    state_.viewStates_[vi].sliceIndices.x = std::clamp(newVoxel.x, 0, state_.volumes_[vi].dimensions.x - 1);
                    state_.viewStates_[vi].sliceIndices.y = std::clamp(newVoxel.y, 0, state_.volumes_[vi].dimensions.y - 1);
                    state_.viewStates_[vi].sliceIndices.z = std::clamp(newVoxel.z, 0, state_.volumes_[vi].dimensions.z - 1);
                }
                viewManager_.updateAllOverlayTextures();
            } else {
                state_.lastSyncSource_ = 0;
                state_.lastSyncView_ = 0;
            }
        }

        if (ImGui::Checkbox("Sync Zoom", &state_.syncZoom_)) {
            if (state_.syncZoom_ && numVolumes > 1) {
                state_.lastSyncSource_ = 0;
                state_.lastSyncView_ = 0;
                for (int v = 0; v < 3; ++v) {
                    viewManager_.syncZoom(0, v);
                }
            }
        }

        if (ImGui::Checkbox("Sync Pan", &state_.syncPan_)) {
            if (state_.syncPan_ && numVolumes > 1) {
                state_.lastSyncSource_ = 0;
                state_.lastSyncView_ = 0;
                for (int v = 0; v < 3; ++v) {
                    viewManager_.syncPan(0, v);
                }
            }
        }

        if (ImGui::Checkbox("Show Crosshairs", &state_.showCrosshairs_)) {
        }

        // View visibility checkboxes
        {
            static const char* viewLabels[3] = {"Axial", "Sagittal", "Coronal"};
            for (int v = 0; v < 3; ++v) {
                ImGui::Checkbox(viewLabels[v], &state_.viewVisible[v]);
            }
            // Prevent all views from being hidden
            int visCount = 0;
            for (int v = 0; v < 3; ++v)
                if (state_.viewVisible[v]) ++visCount;
            if (visCount == 0)
                state_.viewVisible = {true, true, true};
        }

        if (!qcState_.active) {
            ImGui::Text("Tags");
            ImGui::SameLine();
            ImGui::TextDisabled("(always visible)");
        }

        if (ImGui::Button("Save Config", ImVec2(btnWidth, 0))) {
            configFileDialogOpen_ = true;
            configFileDialogIsSave_ = true;
            configFileDialogCurrentPath_ = std::filesystem::current_path().string();
            configFileDialogFilename_ = state_.localConfigPath_.empty() 
                ? "config.json" : std::filesystem::path(state_.localConfigPath_).filename().string();
            updateConfigFileDialogEntries();
        }
        if (ImGui::Button("Load Config", ImVec2(btnWidth, 0))) {
            configFileDialogOpen_ = true;
            configFileDialogIsSave_ = false;
            configFileDialogCurrentPath_ = std::filesystem::current_path().string();
            configFileDialogFilename_.clear();
            updateConfigFileDialogEntries();
        }

        ImGui::Separator();

        if (ImGui::Button("[R] Reset All Views", ImVec2(btnWidth, 0))) {
            viewManager_.resetViews();
            if (hasOverlay)
                viewManager_.updateAllOverlayTextures();
        }

        if (ImGui::Button("[P] Screenshot", ImVec2(btnWidth, 0))) {
            saveScreenshot(backend);
        }

        ImGui::Separator();

        if (ImGui::Button("[C] Clean Mode", ImVec2(btnWidth, 0))) {
            state_.cleanMode_ = true;
        }

        if (ImGui::Button("[?] Hotkeys", ImVec2(btnWidth, 0))) {
            state_.showHotkeysPopup_ = true;
        }

        ImGui::Separator();

        if (ImGui::Button("[Q] Quit", ImVec2(btnWidth, 0))) {
            glfwSetWindowShouldClose(window, true);
        }

        // Embed QC list directly in the Tools panel
        if (qcState_.active)
        {
            ImGui::Separator();

            // --- Prev / Next navigation buttons ---
            {
                float halfW = (btnWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                bool atFirst = (qcState_.currentRowIndex <= 0);
                bool atLast  = (qcState_.currentRowIndex >= qcState_.rowCount() - 1);

                if (atFirst) ImGui::BeginDisabled();
                if (ImGui::Button("<< Prev [", ImVec2(halfW, 0)))
                    switchQCRow(qcState_.currentRowIndex - 1, backend);
                if (atFirst) ImGui::EndDisabled();

                ImGui::SameLine();

                if (atLast) ImGui::BeginDisabled();
                if (ImGui::Button("] Next >>", ImVec2(halfW, 0)))
                    switchQCRow(qcState_.currentRowIndex + 1, backend);
                if (atLast) ImGui::EndDisabled();
            }

            // --- Autosave checkbox + manual Save button ---
            ImGui::Checkbox("Autosave results", &autosave_);
            if (ImGui::Button("Save Results", ImVec2(btnWidth, 0)))
                qcState_.saveOutputCsv();

            // Fill remaining vertical space with a scrollable child
            ImVec2 remaining = ImGui::GetContentRegionAvail();
            ImGui::BeginChild("##qc_list_embed", remaining, ImGuiChildFlags_Borders);
            {
                int numCols = qcState_.columnCount();
                int totalTableCols = 2 + numCols; // # + ID + one per volume column
                int tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
                               | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX;
                if (ImGui::BeginTable("##qc_list", totalTableCols, tableFlags))
                {
                    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
                    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    for (int ci = 0; ci < numCols; ++ci)
                        ImGui::TableSetupColumn(qcState_.columnNames[ci].c_str(),
                            ImGuiTableColumnFlags_WidthFixed, 30.0f);
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableHeadersRow();

                    for (int ri = 0; ri < qcState_.rowCount(); ++ri)
                    {
                        ImGui::TableNextRow();

                        const auto& result = qcState_.results[ri];
                        bool anyFail = false;
                        bool allPass = true;
                        for (int ci = 0; ci < numCols; ++ci)
                        {
                            QCVerdict v = result.verdicts[ci];
                            if (v == QCVerdict::FAIL) anyFail = true;
                            if (v != QCVerdict::PASS) allPass = false;
                        }
                        if (numCols == 0) allPass = false;

                        if (anyFail)
                            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                IM_COL32(180, 40, 40, 60));
                        else if (allPass)
                            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                IM_COL32(40, 180, 40, 60));

                        ImGui::TableSetColumnIndex(0);

                        bool isCurrent = (ri == qcState_.currentRowIndex);
                        char selectId[64];
                        std::snprintf(selectId, sizeof(selectId), "##qc_%d", ri);
                        ImGuiSelectableFlags selFlags = ImGuiSelectableFlags_SpanAllColumns
                                                      | ImGuiSelectableFlags_AllowOverlap;
                        if (ImGui::Selectable(selectId, isCurrent, selFlags))
                            switchQCRow(ri, backend);

                        if (isCurrent && scrollToCurrentRow_)
                        {
                            ImGui::SetScrollHereY();
                            scrollToCurrentRow_ = false;
                        }

                        ImGui::SameLine();
                        ImGui::Text("%d", ri);

                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%s", qcState_.rowIds[ri].c_str());

                        for (int ci = 0; ci < numCols; ++ci)
                        {
                            ImGui::TableSetColumnIndex(2 + ci);
                            QCVerdict v = result.verdicts[ci];
                            if (v == QCVerdict::PASS)
                                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "P");
                            else if (v == QCVerdict::FAIL)
                                ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "F");
                            else
                                ImGui::TextDisabled("-");
                        }
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();
        }
    }

    ImGui::End();
}

 void Interface::renderHotkeyPopup() {
    if (!state_.showHotkeysPopup_)
        return;

    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Hotkeys", &state_.showHotkeysPopup_))
    {
        if (ImGui::BeginTable("##hk", 2))
        {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 0.0f);
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);

            auto row = [](const char* key, const char* action) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(key);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(action);
            };

            row("+/-",       "Slice step");
            row("R",         "Reset views");
            row("C",         "Clean mode");
            row("P",         "Screenshot");
            row("Q",         "Quit");
            row("H / ?",     "Toggle this window");
            if (qcState_.active)
            {
                row("[/]",   "Prev/next QC");
            }

            // Separator row
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Separator();
            ImGui::TableNextColumn(); ImGui::Separator();

            row("L-click",   "Crosshair");
            row("Sh+L-drag", "Pan");
            row("M-drag",    "Scroll slice");
            row("Sh+M-drag", "Zoom");
            row("Wheel",     "Zoom cursor");
            row("R-click",   "Place tag");

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void Interface::renderHotkeyPanel() {
    ImGui::Begin("Hotkeys");
    {
        if (ImGui::BeginTable("##hk", 2))
        {
            ImGui::TableSetupColumn("K", ImGuiTableColumnFlags_WidthFixed, 0.0f);
            ImGui::TableSetupColumn("A", ImGuiTableColumnFlags_WidthStretch);

            auto row = [](const char* key, const char* action) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(key);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(action);
            };

            row("+/-",       "Slice step");
            row("R",         "Reset views");
            row("C",         "Clean mode");
            row("P",         "Screenshot");
            row("Q",         "Quit");
            if (qcState_.active)
            {
                row("[/]",   "Prev/next QC");
            }

            // Separator row
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Separator();
            ImGui::TableNextColumn(); ImGui::Separator();

            row("L-click",   "Crosshair");
            row("Sh+L-drag", "Pan");
            row("M-drag",    "Scroll slice");
            row("Sh+M-drag", "Zoom");
            row("Wheel",     "Zoom cursor");
            row("R-click",   "Place tag");

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

int Interface::renderVolumeColumn(int vi) {
    VolumeViewState& state = state_.viewStates_[vi];
    const Volume& vol = state_.volumes_[vi];
    int viewDirtyMask = 0;

    ImGui::Begin(columnNames_[vi].c_str());
    {
        // Handle missing/failed volumes (placeholders have empty data)
        if (vol.data.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Volume not loaded");
            if (!state_.volumePaths_[vi].empty())
                ImGui::TextWrapped("File: %s", state_.volumePaths_[vi].c_str());
            float viewWidth = ImGui::GetContentRegionAvail().x;
            if (qcState_.active) {
                ImGui::BeginChild("##qc_verdict", ImVec2(viewWidth, 0), ImGuiChildFlags_Borders);
                renderQCVerdictPanel(vi);
                ImGui::EndChild();
            }
            ImGui::End();
            return 0;
        }

        float viewWidth = ImGui::GetContentRegionAvail().x;

        // Render verdict panel at the TOP of each column so it's always visible
        if (qcState_.active)
        {
            ImGui::BeginChild("##qc_verdict_top", ImVec2(viewWidth, 60.0f * state_.dpiScale_),
                              ImGuiChildFlags_Borders);
            renderQCVerdictPanel(vi);
            ImGui::EndChild();
        }

        ImVec2 avail = ImGui::GetContentRegionAvail();

        const float controlsHeightBase = 160.0f * state_.dpiScale_;
        const float controlsHeight = state_.cleanMode_ ? 0.0f : controlsHeightBase;
        float viewAreaHeight = avail.y - controlsHeight;

        // Compute view heights, skipping hidden views and redistributing space
        float viewHeights[3];
        float totalVisibleRatio = 0.0f;
        for (int v = 0; v < 3; ++v)
        {
            if (state_.viewVisible[v])
                totalVisibleRatio += state_.sharedViewRatios[v];
        }
        if (totalVisibleRatio <= 0.0f)
            totalVisibleRatio = 1.0f; // safety

        for (int v = 0; v < 3; ++v)
        {
            if (!state_.viewVisible[v])
            {
                viewHeights[v] = 0.0f;
                continue;
            }
            viewHeights[v] = viewAreaHeight * (state_.sharedViewRatios[v] / totalVisibleRatio);
            if (viewHeights[v] < 40.0f * state_.dpiScale_)
                viewHeights[v] = 40.0f * state_.dpiScale_;
        }

        // Collect indices of visible views for rendering and splitters
        std::vector<int> visibleViews;
        for (int v = 0; v < 3; ++v)
        {
            if (state_.viewVisible[v])
                visibleViews.push_back(v);
        }

        for (size_t i = 0; i < visibleViews.size(); ++i)
        {
            int v = visibleViews[i];
            viewDirtyMask |= renderSliceView(vi, v, ImVec2(viewWidth, viewHeights[v]));

            // Render splitter between consecutive visible views
            if (i + 1 < visibleViews.size())
            {
                int vNext = visibleViews[i + 1];
                ImGuiID splitterId = ImGui::GetID(("##view_splitter_" + std::to_string(v)).c_str());
                float* size1 = &viewHeights[v];
                float* size2 = &viewHeights[vNext];

                constexpr float splitterThick = 4.0f;
                ImRect splitterBb;
                splitterBb.Min = ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y);
                splitterBb.Max = ImVec2(splitterBb.Min.x + viewWidth, splitterBb.Min.y + splitterThick);

                bool changed = ImGui::SplitterBehavior(
                    splitterBb, splitterId, ImGuiAxis_Y,
                    size1, size2,
                    40.0f * state_.dpiScale_,
                    40.0f * state_.dpiScale_
                );

                if (changed)
                {
                    float total = *size1 + *size2;
                    float newRatio1 = *size1 / total;
                    float newRatio2 = *size2 / total;

                    float oldRatio1 = state_.sharedViewRatios[v];
                    float oldRatio2 = state_.sharedViewRatios[vNext];
                    float oldCombined = oldRatio1 + oldRatio2;

                    state_.sharedViewRatios[v] = newRatio1 * oldCombined;
                    state_.sharedViewRatios[vNext] = newRatio2 * oldCombined;
                }

                ImGui::SetCursorScreenPos(ImVec2(splitterBb.Min.x, splitterBb.Min.y + splitterThick));
            }
        }

        for (int v = 0; v < 3; ++v) {
            if (viewDirtyMask & (1 << v)) {
                viewManager_.updateSliceTexture(vi, v);
            }
        }

        if (!state_.cleanMode_) {
            ImGui::BeginChild("##controls", ImVec2(viewWidth, 0), ImGuiChildFlags_Borders);
            {
                if (!qcState_.active && !state_.volumePaths_[vi].empty())
                {
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextDisabled("%s", state_.volumePaths_[vi].c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::Separator();
                }

                if (state_.hasOverlay() && state_.showOverlay_)
                {
                    ImGui::PushID(vi + 3000);
                    ImGui::Text("Alpha:");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    if (ImGui::DragFloat("##alpha", &state_.viewStates_[vi].overlayAlpha,
                                        0.01f, 0.0f, 1.0f, "%.2f"))
                        viewManager_.updateAllOverlayTextures();
                    ImGui::PopID();
                    ImGui::Separator();
                }

                glm::dvec3 worldPos;
                vol.transformVoxelToWorld(state.sliceIndices, worldPos);
                float intensity = vol.get(state.sliceIndices.x, state.sliceIndices.y, state.sliceIndices.z);
                ImGui::Text("V: %d,%d,%d  W: %.1f,%.1f,%.1f  I: %.2f",
                            state.sliceIndices.x, state.sliceIndices.y, state.sliceIndices.z,
                            worldPos.x, worldPos.y, worldPos.z, intensity);

                ImGui::Separator();

                {
                    ImGui::PushID(vi + 1000);

                    static const ColourMapType quickMaps[] = {
                        ColourMapType::Viridis,
                        ColourMapType::GrayScale,
                        ColourMapType::Red,
                        ColourMapType::Green,
                        ColourMapType::Blue,
                        ColourMapType::Spectral,
                    };
                    constexpr int nQuick = std::size(quickMaps);

                    auto applyColourMap = [&](ColourMapType cmType) {
                        state.colourMap = cmType;
                        if (vol.isLabelVolume() && cmType != ColourMapType::GrayScale) {
                            viewManager_.invalidateLabelCache(vi);
                        }
                        viewManager_.updateSliceTexture(vi, 0);
                        viewManager_.updateSliceTexture(vi, 1);
                        viewManager_.updateSliceTexture(vi, 2);
                        if (state_.hasOverlay())
                            viewManager_.updateAllOverlayTextures();
                        if (qcState_.active) {
                            std::string colName = qcState_.columnNames[vi];
                            qcState_.columnConfigs[colName].colourMap = std::string(colourMapName(cmType));
                        }
                    };

                    const float swatchSize = 24.0f * state_.dpiScale_;
                    const float borderThickness = 2.0f * state_.dpiScale_;

                    for (int qi = 0; qi < nQuick; ++qi) {
                        if (qi > 0)
                            ImGui::SameLine();

                        ColourMapType cmType = quickMaps[qi];
                        bool isActive = (state.colourMap == cmType);

                        ImGui::PushID(qi);

                        ImVec2 cursor = ImGui::GetCursorScreenPos();
                        if (ImGui::InvisibleButton("##swatch", ImVec2(swatchSize, swatchSize))) {
                            applyColourMap(cmType);
                        }

                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        ImVec2 pMin = cursor;
                        ImVec2 pMax(cursor.x + swatchSize, cursor.y + swatchSize);

                        // Render all swatches with gradient (using LUT)
                        const ColourLut& lut = colourMapLut(cmType);
                        int nStrips = static_cast<int>(swatchSize);
                        for (int s = 0; s < nStrips; ++s) {
                            float t = static_cast<float>(s) / static_cast<float>(nStrips - 1);
                            int idx = static_cast<int>(t * 255.0f + 0.5f);
                            if (idx > 255)
                                idx = 255;
                            uint32_t packed = lut.table[idx];
                            float x0 = pMin.x + static_cast<float>(s);
                            float x1 = x0 + 1.0f;
                            dl->AddRectFilled(ImVec2(x0, pMin.y), ImVec2(x1, pMax.y), packed);
                        }

                        if (isActive) {
                            dl->AddRect(ImVec2(pMin.x - 1, pMin.y - 1),
                                        ImVec2(pMax.x + 1, pMax.y + 1),
                                        IM_COL32(255, 255, 255, 255), 0.0f, 0, borderThickness);
                        } else {
                            dl->AddRect(pMin, pMax, IM_COL32(80, 80, 80, 255));
                        }

                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s", colourMapName(cmType).data());
                        }

                        ImGui::PopID();
                    }

                    ImGui::SameLine();

                    bool currentInQuick = false;
                    for (int qi = 0; qi < nQuick; ++qi) {
                        if (quickMaps[qi] == state.colourMap) {
                            currentInQuick = true;
                            break;
                        }
                    }

                    const char* moreLabel = currentInQuick
                        ? "More..." : colourMapName(state.colourMap).data();

                    if (ImGui::BeginCombo("##more_maps", moreLabel, ImGuiComboFlags_NoPreview)) {
                        const float dropSwatchW = 80.0f * state_.dpiScale_;
                        const float dropSwatchH = 16.0f * state_.dpiScale_;

                        for (int cm = 0; cm < colourMapCount(); ++cm) {
                            auto cmType = static_cast<ColourMapType>(cm);

                            bool isQuick = false;
                            for (int qi = 0; qi < nQuick; ++qi) {
                                if (quickMaps[qi] == cmType) {
                                    isQuick = true;
                                    break;
                                }
                            }
                            if (isQuick)
                                continue;

                            bool selected = (cmType == state.colourMap);

                            ImGui::PushID(cm);

                            ImVec2 cursor = ImGui::GetCursorScreenPos();
                            if (ImGui::InvisibleButton("##lut", ImVec2(dropSwatchW, dropSwatchH))) {
                                applyColourMap(cmType);
                            }

                            ImDrawList* dl = ImGui::GetWindowDrawList();
                            ImVec2 pMin = cursor;
                            ImVec2 pMax(cursor.x + dropSwatchW, cursor.y + dropSwatchH);

                            const ColourLut& lut = colourMapLut(cmType);
                            int nStrips = static_cast<int>(dropSwatchW);
                            for (int s = 0; s < nStrips; ++s) {
                                float t = static_cast<float>(s) / static_cast<float>(nStrips - 1);
                                int idx = static_cast<int>(t * 255.0f + 0.5f);
                                if (idx > 255)
                                    idx = 255;
                                uint32_t packed = lut.table[idx];
                                float x0 = pMin.x + static_cast<float>(s);
                                float x1 = x0 + 1.0f;
                                dl->AddRectFilled(ImVec2(x0, pMin.y), ImVec2(x1, pMax.y), packed);
                            }

                            if (selected) {
                                dl->AddRect(ImVec2(pMin.x - 1, pMin.y - 1),
                                            ImVec2(pMax.x + 1, pMax.y + 1),
                                            IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f * state_.dpiScale_);
                            } else {
                                dl->AddRect(pMin, pMax, IM_COL32(80, 80, 80, 255));
                            }

                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s", colourMapName(cmType).data());

                            if (selected)
                                ImGui::SetItemDefaultFocus();

                            ImGui::PopID();
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::PopID();
                }

                // Invert colour map checkbox (same line as LUT selector)
                ImGui::SameLine();
                bool invertEnabled = state.invertColourMap;
                if (ImGui::Checkbox("Inv", &invertEnabled))
                {
                    state.invertColourMap = invertEnabled;
                    viewManager_.updateSliceTexture(vi, 0);
                    viewManager_.updateSliceTexture(vi, 1);
                    viewManager_.updateSliceTexture(vi, 2);
                    if (state_.hasOverlay())
                        viewManager_.updateAllOverlayTextures();
                }

                ImGui::Separator();

                bool changed = false;
                ImGui::PushID(vi);
                {
                    auto clampCombo = [&](const char* tooltip, const char* id,
                                          int& mode, bool isUnder) -> bool {
                        bool ret = false;

                        // Wrap everything inside a unique ID scope derived
                        // from the combo id (##under / ##over).  This
                        // guarantees that the preview ColorButton, the
                        // BeginCombo, and all dropdown items live in
                        // non-overlapping ID ranges even when the two combos
                        // share the same colour value.
                        ImGui::PushID(id);

                        // Render preview swatch for closed combo state
                        ImGui::AlignTextToFramePadding();
                        const float clampSwatch = 24.0f * state_.dpiScale_;
                        ImVec2 previewSize(clampSwatch, clampSwatch);
                        uint32_t previewColour = resolveClampColour(
                            mode, state.colourMap, !isUnder);

                        float pr = ((previewColour >>  0) & 0xFF) / 255.0f;
                        float pg = ((previewColour >>  8) & 0xFF) / 255.0f;
                        float pb = ((previewColour >> 16) & 0xFF) / 255.0f;
                        float pa = ((previewColour >> 24) & 0xFF) / 255.0f;

                        ImGui::ColorButton("##preview", ImVec4(pr, pg, pb, pa),
                                           ImGuiColorEditFlags_NoTooltip, previewSize);
                        ImGui::SameLine();

                        // Call BeginCombo (we render our own preview swatch)
                        if (ImGui::BeginCombo("##combo", "", ImGuiComboFlags_NoPreview)) {
                            // Specific colour options
                            struct ColorOption {
                                const char* name;
                                int modeValue;
                                uint32_t colour; // 0xAABBGGRR format
                            };

                            static const ColorOption colorOptions[] = {
                                {"Transparent", kClampTransparent, 0x00000000},
                                {"Current",     kClampCurrent,     0xFFFFFFFF},
                                {"Black",       kClampBlack,       0xFF000000},
                                {"Red",         kClampRed,         0xFF0000FF},
                                {"Green",       kClampGreen,       0xFF00FF00},
                                {"Blue",        kClampBlue,        0xFFFF0000},
                                {"Yellow",      kClampYellow,      0xFF00FFFF},
                                {"White",       kClampWhite,       0xFFFFFFFF},
                            };

                            for (size_t i = 0; i < std::size(colorOptions); ++i) {
                                const auto& opt = colorOptions[i];

                                ImGui::PushID(static_cast<int>(i));

                                ImVec2 swatchSize(clampSwatch, clampSwatch);
                                float sr = ((opt.colour >>  0) & 0xFF) / 255.0f;
                                float sg = ((opt.colour >>  8) & 0xFF) / 255.0f;
                                float sb = ((opt.colour >> 16) & 0xFF) / 255.0f;

                                bool clicked = ImGui::ColorButton(
                                    "##swatch", ImVec4(sr, sg, sb, 1.0f),
                                    ImGuiColorEditFlags_NoTooltip, swatchSize);

                                if (ImGui::IsItemHovered())
                                    ImGui::SetTooltip("%s", opt.name);

                                if (clicked) {
                                    mode = opt.modeValue;
                                    ret = true;
                                }

                                ImGui::PopID();

                                // Visual separators between groups
                                if (i == 1 || i == 2 || i == 5) // after Current, Black, Blue
                                    ImGui::Separator();
                            }

                            ImGui::EndCombo();
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", tooltip);

                        ImGui::PopID();
                        return ret;
                    };

                    float avail = ImGui::GetContentRegionAvail().x;
                    float spacing = ImGui::GetStyle().ItemSpacing.x;
                    float autoW = ImGui::CalcTextSize("Auto").x +
                                  ImGui::GetStyle().FramePadding.x * 2.0f;
                    // Each clamp section = preview swatch + spacing + combo arrow.
                    // The combo arrow width is FrameHeight (square button).
                    float swatchSz = 24.0f * state_.dpiScale_;
                    float arrowW = ImGui::GetFrameHeight();
                    float clampW = arrowW; // SetNextItemWidth for the combo
                    float clampTotal = swatchSz + spacing + clampW; // total visual width per clamp
                    float inputTotal = avail - autoW - clampTotal * 2.0f - spacing * 4.0f;
                    float inputW = inputTotal * 0.5f;
                    if (inputW < 30.0f)
                        inputW = 30.0f;

                    ImGui::SetNextItemWidth(clampW);
                    if (clampCombo("Under colour", "##under", state.underColourMode, true))
                        changed = true;
                    ImGui::SameLine();
                    float dragSpeed = static_cast<float>((vol.max_value - vol.min_value) * 0.001);
                    if (dragSpeed <= 0.0f) dragSpeed = 0.01f;
                    ImGui::SetNextItemWidth(inputW);
                    if (ImGui::DragScalar("##min", ImGuiDataType_Double,
                                         &state.valueRange[0], dragSpeed, nullptr, nullptr, "%.4g"))
                        changed = true;
                    ImGui::SameLine();
                    if (ImGui::Button("Auto")) {
                        state.valueRange[0] = vol.min_value;
                        state.valueRange[1] = vol.max_value;
                        changed = true;
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(inputW);
                    if (ImGui::DragScalar("##max", ImGuiDataType_Double,
                                         &state.valueRange[1], dragSpeed, nullptr, nullptr, "%.4g"))
                        changed = true;
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(clampW);
                    if (clampCombo("Over colour", "##over", state.overColourMode, false))
                        changed = true;
                }
                ImGui::PopID();

                if (changed) {
                    viewManager_.updateSliceTexture(vi, 0);
                    viewManager_.updateSliceTexture(vi, 1);
                    viewManager_.updateSliceTexture(vi, 2);
                    if (state_.hasOverlay())
                        viewManager_.updateAllOverlayTextures();
                    if (qcState_.active) {
                        std::string colName = qcState_.columnNames[vi];
                        qcState_.columnConfigs[colName].valueMin = state.valueRange[0];
                        qcState_.columnConfigs[colName].valueMax = state.valueRange[1];
                    }
                }

                if (ImGui::Button("Reset View")) {
                    for (int v = 0; v < 3; ++v) {
                        state.zoom[v] = 1.0f;
                        state.panU[v] = 0.5f;
                        state.panV[v] = 0.5f;
                    }
                }

                ImGui::Separator();

                // Label volume controls and Log10 transform
                bool isLabel = vol.isLabelVolume();
                bool logEnabled = state.useLogTransform;

                if (ImGui::Checkbox("Label Volume", &isLabel)) {
                    state_.volumes_[vi].setLabelVolume(isLabel);
                    if (isLabel && state.colourMap != ColourMapType::GrayScale) {
                        viewManager_.invalidateLabelCache(vi);
                    }
                    viewManager_.updateSliceTexture(vi, 0);
                    viewManager_.updateSliceTexture(vi, 1);
                    viewManager_.updateSliceTexture(vi, 2);
                    if (state_.hasOverlay())
                        viewManager_.updateAllOverlayTextures();
                }

                ImGui::SameLine();

                // Log10 checkbox (disabled for label volumes)
                if (isLabel)
                    ImGui::BeginDisabled(true);

                if (ImGui::Checkbox("Log10", &logEnabled))
                {
                    state.useLogTransform = logEnabled;
                    viewManager_.updateSliceTexture(vi, 0);
                    viewManager_.updateSliceTexture(vi, 1);
                    viewManager_.updateSliceTexture(vi, 2);
                    if (state_.hasOverlay())
                        viewManager_.updateAllOverlayTextures();
                }

                if (isLabel)
                {
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Log10 unavailable for label volumes");
                }

                // Show current label name at cursor position
                if (isLabel) {
                    std::string labelName = vol.getLabelNameAtVoxel(
                        state.sliceIndices.x, state.sliceIndices.y, state.sliceIndices.z);
                    if (!labelName.empty()) {
                        ImGui::Text("Label: %s", labelName.c_str());
                    }
                }
            }
            ImGui::EndChild();
        }

    }
    ImGui::End();
    return viewDirtyMask;
}

void Interface::renderOverlayPanel() {
    ImGui::Begin("Overlay");
    {
        // In QC mode, add a placeholder to align with verdict panels in volume columns
        if (qcState_.active)
        {
            float placeholderHeight = 60.0f * state_.dpiScale_;
            float viewWidth = ImGui::GetContentRegionAvail().x;
            ImGui::BeginChild("##overlay_placeholder", ImVec2(viewWidth, placeholderHeight),
                              ImGuiChildFlags_Borders);
            ImGui::EndChild();
        }

        ImVec2 avail = ImGui::GetContentRegionAvail();
        const float controlsHeightBase = 160.0f * state_.dpiScale_;
        const float controlsHeight = state_.cleanMode_ ? 0.0f : controlsHeightBase;
        float viewAreaHeight = avail.y - controlsHeight;

        // Compute view heights, skipping hidden views and redistributing space
        float viewHeights[3];
        float totalVisibleRatio = 0.0f;
        for (int v = 0; v < 3; ++v)
        {
            if (state_.viewVisible[v])
                totalVisibleRatio += state_.sharedViewRatios[v];
        }
        if (totalVisibleRatio <= 0.0f)
            totalVisibleRatio = 1.0f; // safety

        for (int v = 0; v < 3; ++v)
        {
            if (!state_.viewVisible[v])
            {
                viewHeights[v] = 0.0f;
                continue;
            }
            viewHeights[v] = viewAreaHeight * (state_.sharedViewRatios[v] / totalVisibleRatio);
            if (viewHeights[v] < 40.0f * state_.dpiScale_)
                viewHeights[v] = 40.0f * state_.dpiScale_;
        }

        // Collect indices of visible views for rendering and splitters
        std::vector<int> visibleViews;
        for (int v = 0; v < 3; ++v)
        {
            if (state_.viewVisible[v])
                visibleViews.push_back(v);
        }

        int overlayDirtyMask = 0;
        for (size_t i = 0; i < visibleViews.size(); ++i)
        {
            int v = visibleViews[i];
            overlayDirtyMask |= renderOverlayView(v, ImVec2(avail.x, viewHeights[v]));

            // Render splitter between consecutive visible views
            if (i + 1 < visibleViews.size())
            {
                int vNext = visibleViews[i + 1];
                ImGuiID splitterId = ImGui::GetID(("##overlay_splitter_" + std::to_string(v)).c_str());
                float* size1 = &viewHeights[v];
                float* size2 = &viewHeights[vNext];

                ImRect splitterBb;
                splitterBb.Min = ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y);
                splitterBb.Max = ImVec2(splitterBb.Min.x + avail.x, splitterBb.Min.y + 8.0f);

                bool changed = ImGui::SplitterBehavior(
                    splitterBb, splitterId, ImGuiAxis_Y,
                    size1, size2,
                    40.0f * state_.dpiScale_,
                    40.0f * state_.dpiScale_
                );

                if (changed)
                {
                    float total = *size1 + *size2;
                    float newRatio1 = *size1 / total;
                    float newRatio2 = *size2 / total;

                    float oldRatio1 = state_.sharedViewRatios[v];
                    float oldRatio2 = state_.sharedViewRatios[vNext];
                    float oldCombined = oldRatio1 + oldRatio2;

                    state_.sharedViewRatios[v] = newRatio1 * oldCombined;
                    state_.sharedViewRatios[vNext] = newRatio2 * oldCombined;
                }

                ImGui::SetCursorScreenPos(ImVec2(splitterBb.Min.x, splitterBb.Min.y + 8.0f));
            }
        }

        if (overlayDirtyMask) {
            for (int vi = 0; vi < state_.volumeCount(); ++vi)
                for (int v = 0; v < 3; ++v)
                    if (overlayDirtyMask & (1 << v))
                        viewManager_.updateSliceTexture(vi, v);
            for (int v = 0; v < 3; ++v)
                if (overlayDirtyMask & (1 << v))
                    viewManager_.updateOverlayTexture(v);
        }

        if (!state_.cleanMode_) {
            ImGui::BeginChild("##overlay_controls", ImVec2(avail.x, 0), ImGuiChildFlags_Borders);
            {
                // Balance slider (2-volume mode only): controls relative alpha between
                // volume 0 and volume 1, synced bidirectionally with the per-volume
                // alpha DragFloat widgets in each volume column panel.
                int numVolumes = state_.volumeCount();
                if (numVolumes == 2)
                {
                    float a0 = state_.viewStates_[0].overlayAlpha;
                    float a1 = state_.viewStates_[1].overlayAlpha;
                    float blendT = (a0 + a1 > 0.0f) ? a1 / (a0 + a1) : 0.5f;
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    if (ImGui::SliderFloat("##blend", &blendT, 0.0f, 1.0f, "Balance %.2f"))
                    {
                        state_.viewStates_[0].overlayAlpha = 1.0f - blendT;
                        state_.viewStates_[1].overlayAlpha = blendT;
                        viewManager_.updateAllOverlayTextures();
                    }
                }

                bool anyVolumeHasTags = false;
                for (const auto& vol : state_.volumes_) {
                    if (vol.hasTags()) {
                        anyVolumeHasTags = true;
                        break;
                    }
                }
                if (anyVolumeHasTags && ImGui::Checkbox("Show Tags", &state_.tagsVisible_)) {
                }

                if (ImGui::Button("Reset View")) {
                    for (int v = 0; v < 3; ++v) {
                        state_.overlay_.zoom[v] = 1.0f;
                        state_.overlay_.panU[v] = 0.5f;
                        state_.overlay_.panV[v] = 0.5f;
                    }
                }
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();
}

void Interface::renderTagListWindow() {
    ImGui::Begin("Tags", &state_.tagListWindowVisible_);
    renderTagListContent();
    ImGui::End();
}

void Interface::renderTagListContent() {
    {
        int numVolumes = state_.volumeCount();
        if (numVolumes == 0 || static_cast<int>(state_.volumeNames_.size()) < numVolumes) {
            ImGui::Text("No volumes loaded");
            return;
        }

        int maxTags = state_.getMaxTagCount();
        float w = ImGui::GetContentRegionAvail().x;
        float btnW = (w - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        // --- Tag Mode toggle ---
        {
            bool isEditMode = (state_.selectedTagIndex_ >= 0);
            if (isEditMode) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                if (ImGui::Button("Edit Mode (right-click to move)", ImVec2(w, 0))) {
                    state_.selectedTagIndex_ = -1;
                }
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
                if (ImGui::Button("Add Mode (right-click to add)", ImVec2(w, 0))) {
                }
                ImGui::PopStyleColor();
            }
        }

        ImGui::Spacing();

        // --- Tag file Load/Save buttons ---
        if (ImGui::Button("Load Tags", ImVec2(w * 0.5f - 4.0f, 0))) {
            tagFileDialogOpen_ = true;
            tagFileDialogIsSave_ = false;
            tagFileDialogCurrentPath_ = std::filesystem::current_path().string();
            tagFileDialogFilename_.clear();
            updateTagFileDialogEntries();
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Tags", ImVec2(w * 0.5f - 4.0f, 0))) {
            tagFileDialogOpen_ = true;
            tagFileDialogIsSave_ = true;
            tagFileDialogCurrentPath_ = std::filesystem::current_path().string();
            tagFileDialogFilename_ = "output.tag";
            updateTagFileDialogEntries();
        }

        // --- Display currently loaded tag file (two-volume mode) ---
        if (numVolumes >= 2) {
            if (state_.usePerVolumeTagFiles_) {
                ImGui::TextWrapped("Mode: per-volume tag files");
                for (int vi = 0; vi < numVolumes; ++vi) {
                    if (!state_.volumePaths_[vi].empty()) {
                        std::filesystem::path tagPath(state_.volumePaths_[vi]);
                        tagPath.replace_extension(".tag");
                        ImGui::TextWrapped("  Vol %d: %s", vi, tagPath.filename().string().c_str());
                    }
                }
            } else if (state_.combinedTagPath_[0] != '\0') {
                std::filesystem::path p(state_.combinedTagPath_);
                ImGui::TextWrapped("Tag file: %s", p.filename().string().c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", state_.combinedTagPath_);
                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "No tag file loaded");
            }

            ImGui::Checkbox("Per-volume tag files", &state_.usePerVolumeTagFiles_);
        }

        ImGui::Spacing();
        ImGui::Checkbox("Auto-save Tags", &state_.autoSaveTags_);

        // --- Transform section (only with 2+ volumes) ---
        if (numVolumes >= 2) {
            ImGui::Separator();

            int currentType = static_cast<int>(state_.transformType_);
            static const char* transformLabels[] = {
                "LSQ6 (Rigid)", "LSQ7 (Similarity)", "LSQ9", "LSQ10", "LSQ12", "TPS"
            };
            ImGui::SetNextItemWidth(w);
            if (ImGui::Combo("Transform", &currentType, transformLabels, IM_ARRAYSIZE(transformLabels))) {
                state_.setTransformType(static_cast<TransformType>(currentType));
                if (state_.hasOverlay())
                    viewManager_.updateAllOverlayTextures();
            }

            const TransformResult& result = state_.transformResult_;

            if (result.valid) {
                ImGui::Text("Avg RMS: %.6f", result.avgRMS);

                // Save .xfm path
                ImGui::SetNextItemWidth(w - 70.0f * state_.dpiScale_);
                ImGui::InputText("##xfm_path", state_.xfmFilePath_, sizeof(state_.xfmFilePath_));
                ImGui::SameLine();
                if (ImGui::Button("Save XFM", ImVec2(70.0f * state_.dpiScale_, 0))) {
                    if (writeXfmFile(state_.xfmFilePath_, result)) {
                        std::cout << "Saved transform to " << state_.xfmFilePath_ << "\n";
                    } else {
                        std::cerr << "Failed to save transform\n";
                    }
                }
            } else {
                ImGui::TextColored(ImVec4(1,0.5,0,1), "Need %d+ tag pairs",
                    state_.transformType_ == TransformType::TPS ? kMinPointsTPS : kMinPointsLinear);
            }
        }

        ImGui::Separator();

        // --- Tag list or status ---
        if (maxTags == 0) {
            ImGui::Text("No tags loaded");
        } else {
            // Delete button
            if (ImGui::Button("Delete Selected", ImVec2(w, 0))) {
                if (state_.selectedTagIndex_ >= 0 && state_.selectedTagIndex_ < maxTags) {
                    for (int vi = 0; vi < numVolumes; ++vi) {
                        if (state_.selectedTagIndex_ < state_.volumes_[vi].getTagCount()) {
                            state_.volumes_[vi].tags.removeTag(state_.selectedTagIndex_);
                        }
                    }
                    if (state_.autoSaveTags_) state_.saveTags();
                    state_.selectedTagIndex_ = -1;
                    state_.invalidateTransform();
                    state_.recomputeTransform();
                    if (state_.hasOverlay())
                        viewManager_.updateAllOverlayTextures();
                }
            }

            ImGui::Spacing();

            bool hasRMS = (numVolumes >= 2 && state_.transformResult_.valid &&
                           !state_.transformResult_.perTagRMS.empty());
            int numColumns = 2 + numVolumes + (hasRMS ? 1 : 0);
            int tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit;
            if (ImGui::BeginTable("##tags_table", numColumns, tableFlags)) {
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                for (int vi = 0; vi < numVolumes; ++vi) {
                    ImGui::TableSetupColumn(state_.volumeNames_[vi].c_str(), ImGuiTableColumnFlags_WidthFixed, 120.0f);
                }
                if (hasRMS) {
                    ImGui::TableSetupColumn("RMS", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                }
                ImGui::TableHeadersRow();

                for (int ti = 0; ti < maxTags; ++ti) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%d", ti);

                    ImGui::TableSetColumnIndex(1);
                    std::string labelText;
                    for (int vi = 0; vi < numVolumes && labelText.empty(); ++vi) {
                        if (ti < static_cast<int>(state_.volumes_[vi].getTagLabels().size())) {
                            labelText = state_.volumes_[vi].getTagLabels()[ti];
                        }
                    }
                    ImGui::Text("%s", labelText.empty() ? "-" : labelText.c_str());

                    for (int vi = 0; vi < numVolumes; ++vi) {
                        ImGui::TableSetColumnIndex(2 + vi);
                        if (ti < state_.volumes_[vi].getTagCount()) {
                            glm::dvec3 worldPos = state_.volumes_[vi].getTagPoints()[ti];
                            ImGui::Text("%.1f,%.1f,%.1f", worldPos.x, worldPos.y, worldPos.z);
                        } else {
                            ImGui::Text("-");
                        }
                    }

                    if (hasRMS) {
                        ImGui::TableSetColumnIndex(2 + numVolumes);
                        if (ti < static_cast<int>(state_.transformResult_.perTagRMS.size())) {
                            ImGui::Text("%.4f", state_.transformResult_.perTagRMS[ti]);
                        } else {
                            ImGui::Text("-");
                        }
                    }

                    ImGui::TableSetColumnIndex(0);
                    ImGuiSelectableFlags selectableFlags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;
                    char selectableId[64];
                    std::snprintf(selectableId, sizeof(selectableId), "##select_tag_%d", ti);
                    if (ImGui::Selectable(selectableId, state_.selectedTagIndex_ == ti, selectableFlags)) {
                        state_.setSelectedTag(ti);
                        for (int v = 0; v < numVolumes; ++v) {
                            for (int vv = 0; vv < 3; ++vv) {
                                viewManager_.updateSliceTexture(v, vv);
                            }
                        }
                        if (state_.hasOverlay()) {
                            viewManager_.updateAllOverlayTextures();
                        }
                    }
                }
                ImGui::EndTable();
            }
        }
    }
}

int Interface::renderSliceView(int vi, int viewIndex, const ImVec2& childSize) {
    int dirtyMask = 0;
    char childId[64];
    std::snprintf(childId, sizeof(childId), "##view_%d_%d", vi, viewIndex);

    VolumeViewState& state = state_.viewStates_[vi];
    const Volume& vol = state_.volumes_[vi];

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild(childId, childSize, ImGuiChildFlags_None);
    ImGui::PopStyleVar();
    {
        if (state.sliceTextures[viewIndex]) {
            Texture* tex = state.sliceTextures[viewIndex].get();
            ImVec2 avail = ImGui::GetContentRegionAvail();

            ImVec2 imgPos(0, 0);
            ImVec2 imgSize(0, 0);

            if (avail.x > 0 && avail.y > 0) {
                int axisU, axisV;
                if (viewIndex == 0) {
                    axisU = 0;
                    axisV = 1;
                } else if (viewIndex == 1) {
                    axisU = 1;
                    axisV = 2;
                } else {
                    axisU = 0;
                    axisV = 2;
                }

                double pixelAspect = vol.slicePixelAspect(axisU, axisV);
                float aspect = static_cast<float>(tex->width) /
                               static_cast<float>(tex->height) *
                               static_cast<float>(pixelAspect);

                // Base image size at zoom=1: letterboxed fit to panel
                ImVec2 base = avail;
                if (base.x / base.y > aspect)
                    base.x = base.y * aspect;
                else
                    base.y = base.x / aspect;

                // When zoomed in, expand the image to fill more of the
                // available panel space (up to the panel edges).  This
                // lets the zoomed slice use all available pixels instead
                // of staying inside the zoom=1 letterbox.
                float zf = static_cast<float>(state.zoom[viewIndex]);
                imgSize = base;
                if (zf > 1.0f)
                {
                    imgSize.x = std::min(base.x * zf, avail.x);
                    imgSize.y = std::min(base.y * zf, avail.y);
                }

                // Center the image in the panel
                float padX = (avail.x - imgSize.x) * 0.5f;
                float padY = (avail.y - imgSize.y) * 0.5f;
                if (padX > 0)
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padX);
                if (padY > 0)
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padY);

                imgPos = ImGui::GetCursorScreenPos();

                // UV span: fraction of texture visible in each axis.
                // At zoom=1 with base==imgSize: halfU = halfV = 0.5 (full texture).
                // At zoom>1: the image may have expanded beyond base, so the
                // UV span widens proportionally to show more context.
                float halfU = 0.5f * imgSize.x / (base.x * zf);
                float halfV = 0.5f * imgSize.y / (base.y * zf);
                float centerU = state.panU[viewIndex];
                float centerV = state.panV[viewIndex];
                ImVec2 uv0(centerU - halfU, centerV - halfV);
                ImVec2 uv1(centerU + halfU, centerV + halfV);

                ImGui::Image(
                    tex->id,
                    imgSize, uv0, uv1);

                if (state_.showCrosshairs_) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    const ImU32 crossCol = IM_COL32(255, 255, 0, 100);
                    const float crossThick = 1.0f * state_.dpiScale_;

                    float normCrossU = 0.0f, normCrossV = 0.0f;
                    if (viewIndex == 0) {
                        normCrossU = static_cast<float>(state.sliceIndices.x) /
                                     static_cast<float>(std::max(vol.dimensions.x - 1, 1));
                        normCrossV = static_cast<float>(state.sliceIndices.y) /
                                     static_cast<float>(std::max(vol.dimensions.y - 1, 1));
                    } else if (viewIndex == 1) {
                        normCrossU = static_cast<float>(state.sliceIndices.y) /
                                     static_cast<float>(std::max(vol.dimensions.y - 1, 1));
                        normCrossV = static_cast<float>(state.sliceIndices.z) /
                                     static_cast<float>(std::max(vol.dimensions.z - 1, 1));
                    } else {
                        normCrossU = static_cast<float>(state.sliceIndices.x) /
                                     static_cast<float>(std::max(vol.dimensions.x - 1, 1));
                        normCrossV = static_cast<float>(state.sliceIndices.z) /
                                     static_cast<float>(std::max(vol.dimensions.z - 1, 1));
                    }

                    normCrossV = 1.0f - normCrossV;

                    float uvSpanU = uv1.x - uv0.x;
                    float uvSpanV = uv1.y - uv0.y;
                    float screenX = imgPos.x + (normCrossU - uv0.x) / uvSpanU * imgSize.x;
                    float screenY = imgPos.y + (normCrossV - uv0.y) / uvSpanV * imgSize.y;

                    ImVec2 clipMin = imgPos;
                    ImVec2 clipMax(imgPos.x + imgSize.x, imgPos.y + imgSize.y);
                    dl->PushClipRect(clipMin, clipMax, true);

                    dl->AddLine(ImVec2(screenX, imgPos.y),
                                ImVec2(screenX, imgPos.y + imgSize.y),
                                crossCol, crossThick);
                    dl->AddLine(ImVec2(imgPos.x, screenY),
                                ImVec2(imgPos.x + imgSize.x, screenY),
                                crossCol, crossThick);

                    dl->PopClipRect();
                }

                drawTagsOnSlice(viewIndex, imgPos, imgSize, uv0, uv1, vol, state.sliceIndices, state_.selectedTagIndex_);

                bool imageHovered = ImGui::IsItemHovered();
                bool shiftHeld = ImGui::GetIO().KeyShift;

                // Track last-clicked view for keyboard +/- slice navigation
                if (imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    state_.activeView_ = viewIndex;

                if (imageHovered && shiftHeld &&
                    ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    float uvSpanU = uv1.x - uv0.x;
                    float uvSpanV = uv1.y - uv0.y;
                    state.panU[viewIndex] -= delta.x / imgSize.x * uvSpanU;
                    state.panV[viewIndex] -= delta.y / imgSize.y * uvSpanV;
                    if (state_.syncPan_) {
                        state_.lastSyncSource_ = vi;
                        state_.lastSyncView_ = viewIndex;
                        viewManager_.syncPan(vi, viewIndex);
                    }
                } else if (imageHovered && !shiftHeld &&
                           ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    ImVec2 mouse = ImGui::GetMousePos();
                    float normU = uv0.x + (mouse.x - imgPos.x) / imgSize.x * (uv1.x - uv0.x);
                    float normV = uv0.y + (mouse.y - imgPos.y) / imgSize.y * (uv1.y - uv0.y);
                    normU = std::clamp(normU, 0.0f, 1.0f);
                    normV = std::clamp(normV, 0.0f, 1.0f);
                    normV = 1.0f - normV;

                    if (viewIndex == 0) {
                        int voxX = static_cast<int>(normU * (vol.dimensions.x - 1) + 0.5f);
                        int voxY = static_cast<int>(normV * (vol.dimensions.y - 1) + 0.5f);
                        state.sliceIndices.x = std::clamp(voxX, 0, vol.dimensions.x - 1);
                        state.sliceIndices.y = std::clamp(voxY, 0, vol.dimensions.y - 1);
                        dirtyMask |= (1 << 1) | (1 << 2);
                        if (state_.syncCursors_) {
                            state_.lastSyncSource_ = vi;
                            state_.lastSyncView_ = viewIndex;
                            state_.cursorSyncDirty_ = true;
                        }
                    } else if (viewIndex == 1) {
                        int voxY = static_cast<int>(normU * (vol.dimensions.y - 1) + 0.5f);
                        int voxZ = static_cast<int>(normV * (vol.dimensions.z - 1) + 0.5f);
                        state.sliceIndices.y = std::clamp(voxY, 0, vol.dimensions.y - 1);
                        state.sliceIndices.z = std::clamp(voxZ, 0, vol.dimensions.z - 1);
                        dirtyMask |= (1 << 0) | (1 << 2);
                        if (state_.syncCursors_) {
                            state_.lastSyncSource_ = vi;
                            state_.lastSyncView_ = viewIndex;
                            state_.cursorSyncDirty_ = true;
                        }
                    } else {
                        int voxX = static_cast<int>(normU * (vol.dimensions.x - 1) + 0.5f);
                        int voxZ = static_cast<int>(normV * (vol.dimensions.z - 1) + 0.5f);
                        state.sliceIndices.x = std::clamp(voxX, 0, vol.dimensions.x - 1);
                        state.sliceIndices.z = std::clamp(voxZ, 0, vol.dimensions.z - 1);
                        dirtyMask |= (1 << 0) | (1 << 1);
                        if (state_.syncCursors_) {
                            state_.lastSyncSource_ = vi;
                            state_.lastSyncView_ = viewIndex;
                            state_.cursorSyncDirty_ = true;
                        }
                    }
                }

                if (imageHovered && shiftHeld &&
                    ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
                    float dragY = ImGui::GetIO().MouseDelta.y;
                    if (dragY != 0.0f) {
                        double factor = 1.0 - dragY * 0.005;
                        state.zoom[viewIndex] = std::clamp(
                            state.zoom[viewIndex] * factor, 0.1, 50.0);
                        if (state_.syncZoom_) {
                            state_.lastSyncSource_ = vi;
                            state_.lastSyncView_ = viewIndex;
                            viewManager_.syncZoom(vi, viewIndex);
                        }
                    }
                } else if (imageHovered && !shiftHeld &&
                           ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
                    float dragY = ImGui::GetIO().MouseDelta.y;
                    if (dragY != 0.0f) {
                        int maxSliceVal = 0;
                        if (viewIndex == 0) {
                            maxSliceVal = vol.dimensions.z;
                        } else if (viewIndex == 1) {
                            maxSliceVal = vol.dimensions.x;
                        } else {
                            maxSliceVal = vol.dimensions.y;
                        }
                        float sliceDelta = -dragY / imgSize.y * static_cast<float>(maxSliceVal);
                        state.dragAccum[viewIndex] += sliceDelta;
                        int steps = static_cast<int>(state.dragAccum[viewIndex]);
                        if (steps != 0) {
                            state.dragAccum[viewIndex] -= static_cast<float>(steps);
                            if (viewIndex == 0) {
                                state.sliceIndices.z = std::clamp(
                                    state.sliceIndices.z + steps, 0, maxSliceVal - 1);
                            } else if (viewIndex == 1) {
                                state.sliceIndices.x = std::clamp(
                                    state.sliceIndices.x + steps, 0, maxSliceVal - 1);
                            } else {
                                state.sliceIndices.y = std::clamp(
                                    state.sliceIndices.y + steps, 0, maxSliceVal - 1);
                            }
                            dirtyMask |= (1 << viewIndex);
                            if (state_.syncCursors_) {
                                state_.lastSyncSource_ = vi;
                                state_.lastSyncView_ = viewIndex;
                                state_.cursorSyncDirty_ = true;
                            }
                        }
                    }
                } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                    state.dragAccum[viewIndex] = 0.0f;
                }

                if (imageHovered) {
                    float wheel = ImGui::GetIO().MouseWheel;
                    if (wheel != 0.0f) {
                        ImVec2 mouse = ImGui::GetMousePos();
                        float cursorU = uv0.x + (mouse.x - imgPos.x) / imgSize.x * (uv1.x - uv0.x);
                        float cursorV = uv0.y + (mouse.y - imgPos.y) / imgSize.y * (uv1.y - uv0.y);

                        double factor = (wheel > 0) ? 1.1 : 1.0 / 1.1;
                        double newZoom = std::clamp(
                            state.zoom[viewIndex] * factor, 0.1, 50.0);

                        double zOld = state.zoom[viewIndex];
                        state.panU[viewIndex] = cursorU +
                            (state.panU[viewIndex] - cursorU) * (zOld / newZoom);
                        state.panV[viewIndex] = cursorV +
                            (state.panV[viewIndex] - cursorV) * (zOld / newZoom);
                        state.zoom[viewIndex] = newZoom;

                        if (state_.syncZoom_ || state_.syncPan_) {
                            state_.lastSyncSource_ = vi;
                            state_.lastSyncView_ = viewIndex;
                            if (state_.syncZoom_)
                                viewManager_.syncZoom(vi, viewIndex);
                            if (state_.syncPan_)
                                viewManager_.syncPan(vi, viewIndex);
                        }
                    }

                    if (!qcState_.active && imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        Volume& curVol = state_.volumes_[vi];
                        glm::ivec3 volCursorPos = state_.viewStates_[vi].sliceIndices;
                        glm::dvec3 volWorldPos;
                        curVol.transformVoxelToWorld(volCursorPos, volWorldPos);

                        if (state_.selectedTagIndex_ >= 0) {
                            // EDIT mode: update selected tag position
                            if (state_.selectedTagIndex_ < curVol.getTagCount()) {
                                // Tag exists at this index - update position
                                curVol.tags.updateTag(state_.selectedTagIndex_, volWorldPos, "");
                            } else {
                                // Tag doesn't exist in this volume - create new tag at cursor
                                auto pts = curVol.getTagPoints();
                                auto lbls = curVol.getTagLabels();
                                std::string newLabel = "Point" + std::to_string(state_.selectedTagIndex_ + 1);
                                pts.push_back(volWorldPos);
                                lbls.push_back(newLabel);
                                curVol.tags.setPoints(pts);
                                curVol.tags.setLabels(lbls);
                            }
                            state_.invalidateTransform();
                            state_.recomputeTransform();
                            if (state_.autoSaveTags_) state_.saveTags();
                            for (int vv = 0; vv < 3; ++vv) {
                                viewManager_.updateSliceTexture(vi, vv);
                            }
                            if (state_.hasOverlay()) {
                                viewManager_.updateAllOverlayTextures();
                            }
                        } else {
                            // CREATE mode (existing behavior): create new tag for all volumes
                            int tagCount = state_.volumes_[0].getTagCount();
                            std::string newLabel = "Point" + std::to_string(tagCount + 1);

                            for (int v = 0; v < state_.volumeCount(); ++v) {
                                Volume& vol = state_.volumes_[v];
                                glm::ivec3 cursorPos = state_.viewStates_[v].sliceIndices;
                                glm::dvec3 worldPos;
                                vol.transformVoxelToWorld(cursorPos, worldPos);

                                auto pts = vol.getTagPoints();
                                auto lbls = vol.getTagLabels();
                                pts.push_back(worldPos);
                                lbls.push_back(newLabel);
                                vol.tags.setPoints(pts);
                                vol.tags.setLabels(lbls);
                            }

                            if (state_.autoSaveTags_) state_.saveTags();
                            state_.invalidateTransform();
                            state_.recomputeTransform();
                            for (int v = 0; v < state_.volumeCount(); ++v) {
                                for (int vv = 0; vv < 3; ++vv) {
                                    viewManager_.updateSliceTexture(v, vv);
                                }
                            }
                            if (state_.hasOverlay()) {
                                viewManager_.updateAllOverlayTextures();
                            }
                        }
                    }
                }
            }
        }
    }
    ImGui::EndChild();
    return dirtyMask;
}

int Interface::renderOverlayView(int viewIndex, const ImVec2& childSize) {
    int dirtyMask = 0;
    const Volume& ref = state_.volumes_[0];
    VolumeViewState& refState = state_.viewStates_[0];

    char childId[64];
    std::snprintf(childId, sizeof(childId), "##overlay_%d", viewIndex);

    ImGui::BeginChild(childId, childSize, ImGuiChildFlags_Borders);
    {
        if (state_.overlay_.textures[viewIndex]) {
            Texture* tex = state_.overlay_.textures[viewIndex].get();
            ImVec2 avail = ImGui::GetContentRegionAvail();

            ImVec2 imgPos(0, 0);
            ImVec2 imgSize(0, 0);

            if (avail.x > 0 && avail.y > 0) {
                int axisU, axisV;
                if (viewIndex == 0) {
                    axisU = 0;
                    axisV = 1;
                } else if (viewIndex == 1) {
                    axisU = 1;
                    axisV = 2;
                } else {
                    axisU = 0;
                    axisV = 2;
                }

                double pixelAspect = ref.slicePixelAspect(axisU, axisV);
                float aspect = static_cast<float>(tex->width) /
                               static_cast<float>(tex->height) *
                               static_cast<float>(pixelAspect);

                // Base image size at zoom=1: letterboxed fit to panel
                ImVec2 base = avail;
                if (base.x / base.y > aspect)
                    base.x = base.y * aspect;
                else
                    base.y = base.x / aspect;

                // When zoomed in, expand the image to fill more of the
                // available panel space (up to the panel edges).
                float zf = static_cast<float>(state_.overlay_.zoom[viewIndex]);
                imgSize = base;
                if (zf > 1.0f)
                {
                    imgSize.x = std::min(base.x * zf, avail.x);
                    imgSize.y = std::min(base.y * zf, avail.y);
                }

                // Center the image in the panel
                float padX = (avail.x - imgSize.x) * 0.5f;
                float padY = (avail.y - imgSize.y) * 0.5f;
                if (padX > 0)
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padX);
                if (padY > 0)
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padY);

                imgPos = ImGui::GetCursorScreenPos();

                // UV span: fraction of texture visible in each axis
                float halfU = 0.5f * imgSize.x / (base.x * zf);
                float halfV = 0.5f * imgSize.y / (base.y * zf);
                float centerU = state_.overlay_.panU[viewIndex];
                float centerV = state_.overlay_.panV[viewIndex];
                ImVec2 uv0(centerU - halfU, centerV - halfV);
                ImVec2 uv1(centerU + halfU, centerV + halfV);

                ImGui::Image(
                    tex->id,
                    imgSize, uv0, uv1);

                if (state_.showCrosshairs_) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    const ImU32 crossCol = IM_COL32(255, 255, 0, 100);
                    const float crossThick = 1.0f * state_.dpiScale_;

                    float normCrossU = 0.0f, normCrossV = 0.0f;
                    if (viewIndex == 0) {
                        normCrossU = static_cast<float>(refState.sliceIndices.x) /
                                     static_cast<float>(std::max(ref.dimensions.x - 1, 1));
                        normCrossV = static_cast<float>(refState.sliceIndices.y) /
                                     static_cast<float>(std::max(ref.dimensions.y - 1, 1));
                    } else if (viewIndex == 1) {
                        normCrossU = static_cast<float>(refState.sliceIndices.y) /
                                     static_cast<float>(std::max(ref.dimensions.y - 1, 1));
                        normCrossV = static_cast<float>(refState.sliceIndices.z) /
                                     static_cast<float>(std::max(ref.dimensions.z - 1, 1));
                    } else {
                        normCrossU = static_cast<float>(refState.sliceIndices.x) /
                                     static_cast<float>(std::max(ref.dimensions.x - 1, 1));
                        normCrossV = static_cast<float>(refState.sliceIndices.z) /
                                     static_cast<float>(std::max(ref.dimensions.z - 1, 1));
                    }

                    normCrossV = 1.0f - normCrossV;

                    float uvSpanU = uv1.x - uv0.x;
                    float uvSpanV = uv1.y - uv0.y;
                    float screenX = imgPos.x + (normCrossU - uv0.x) / uvSpanU * imgSize.x;
                    float screenY = imgPos.y + (normCrossV - uv0.y) / uvSpanV * imgSize.y;

                    ImVec2 clipMin = imgPos;
                    ImVec2 clipMax(imgPos.x + imgSize.x, imgPos.y + imgSize.y);
                    dl->PushClipRect(clipMin, clipMax, true);

                    dl->AddLine(ImVec2(screenX, imgPos.y),
                                ImVec2(screenX, imgPos.y + imgSize.y),
                                crossCol, crossThick);
                    dl->AddLine(ImVec2(imgPos.x, screenY),
                                ImVec2(imgPos.x + imgSize.x, screenY),
                                crossCol, crossThick);

                    dl->PopClipRect();
                }

                bool imageHovered = ImGui::IsItemHovered();
                bool shiftHeld = ImGui::GetIO().KeyShift;

                // Track last-clicked view for keyboard +/- slice navigation
                if (imageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    state_.activeView_ = viewIndex;

                if (imageHovered && shiftHeld &&
                    ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    float uvSpanU = uv1.x - uv0.x;
                    float uvSpanV = uv1.y - uv0.y;
                    state_.overlay_.panU[viewIndex] -= delta.x / imgSize.x * uvSpanU;
                    state_.overlay_.panV[viewIndex] -= delta.y / imgSize.y * uvSpanV;
                    if (state_.syncPan_) {
                        state_.lastSyncSource_ = -1;
                        state_.lastSyncView_ = viewIndex;
                        viewManager_.syncPan(-1, viewIndex);
                    }
                } else if (imageHovered && !shiftHeld &&
                           ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    ImVec2 mouse = ImGui::GetMousePos();
                    float normU = uv0.x + (mouse.x - imgPos.x) / imgSize.x * (uv1.x - uv0.x);
                    float normV = uv0.y + (mouse.y - imgPos.y) / imgSize.y * (uv1.y - uv0.y);
                    normU = std::clamp(normU, 0.0f, 1.0f);
                    normV = std::clamp(normV, 0.0f, 1.0f);
                    normV = 1.0f - normV;

                    if (viewIndex == 0) {
                        int voxX = static_cast<int>(normU * (ref.dimensions.x - 1) + 0.5f);
                        int voxY = static_cast<int>(normV * (ref.dimensions.y - 1) + 0.5f);
                        for (auto& st : state_.viewStates_) {
                            st.sliceIndices.x = std::clamp(voxX, 0, ref.dimensions.x - 1);
                            st.sliceIndices.y = std::clamp(voxY, 0, ref.dimensions.y - 1);
                        }
                        dirtyMask |= (1 << 1) | (1 << 2);
                    } else if (viewIndex == 1) {
                        int voxY = static_cast<int>(normU * (ref.dimensions.y - 1) + 0.5f);
                        int voxZ = static_cast<int>(normV * (ref.dimensions.z - 1) + 0.5f);
                        for (auto& st : state_.viewStates_) {
                            st.sliceIndices.y = std::clamp(voxY, 0, ref.dimensions.y - 1);
                            st.sliceIndices.z = std::clamp(voxZ, 0, ref.dimensions.z - 1);
                        }
                        dirtyMask |= (1 << 0) | (1 << 2);
                    } else {
                        int voxX = static_cast<int>(normU * (ref.dimensions.x - 1) + 0.5f);
                        int voxZ = static_cast<int>(normV * (ref.dimensions.z - 1) + 0.5f);
                        for (auto& st : state_.viewStates_) {
                            st.sliceIndices.x = std::clamp(voxX, 0, ref.dimensions.x - 1);
                            st.sliceIndices.z = std::clamp(voxZ, 0, ref.dimensions.z - 1);
                        }
                        dirtyMask |= (1 << 0) | (1 << 1);
                    }
                }

                if (imageHovered && shiftHeld &&
                    ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
                    float dragY = ImGui::GetIO().MouseDelta.y;
                    if (dragY != 0.0f) {
                        double factor = 1.0 - dragY * 0.005;
                        state_.overlay_.zoom[viewIndex] = std::clamp(
                            state_.overlay_.zoom[viewIndex] * factor, 0.1, 50.0);
                        if (state_.syncZoom_) {
                            state_.lastSyncSource_ = -1;
                            state_.lastSyncView_ = viewIndex;
                            viewManager_.syncZoom(-1, viewIndex);
                        }
                    }
                } else if (imageHovered && !shiftHeld &&
                           ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
                    float dragY = ImGui::GetIO().MouseDelta.y;
                    if (dragY != 0.0f) {
                        int maxSliceVal = (viewIndex == 0) ? ref.dimensions.z
                                            : (viewIndex == 1) ? ref.dimensions.x
                                                               : ref.dimensions.y;
                        float sliceDelta = -dragY / imgSize.y * static_cast<float>(maxSliceVal);
                        state_.overlay_.dragAccum[viewIndex] += sliceDelta;
                        int steps = static_cast<int>(state_.overlay_.dragAccum[viewIndex]);
                        if (steps != 0) {
                            state_.overlay_.dragAccum[viewIndex] -= static_cast<float>(steps);
                            int refSlice = (viewIndex == 0) ? refState.sliceIndices.z
                                            : (viewIndex == 1) ? refState.sliceIndices.x
                                                               : refState.sliceIndices.y;
                            int newSlice = std::clamp(refSlice + steps, 0, maxSliceVal - 1);
                            for (auto& st : state_.viewStates_) {
                                if (viewIndex == 0)
                                    st.sliceIndices.z = newSlice;
                                else if (viewIndex == 1)
                                    st.sliceIndices.x = newSlice;
                                else
                                    st.sliceIndices.y = newSlice;
                            }
                            dirtyMask |= (1 << viewIndex);
                        }
                    }
                } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                    state_.overlay_.dragAccum[viewIndex] = 0.0f;
                }

                if (imageHovered) {
                    float wheel = ImGui::GetIO().MouseWheel;
                    if (wheel != 0.0f) {
                        ImVec2 mouse = ImGui::GetMousePos();
                        float cursorU = uv0.x + (mouse.x - imgPos.x) / imgSize.x * (uv1.x - uv0.x);
                        float cursorV = uv0.y + (mouse.y - imgPos.y) / imgSize.y * (uv1.y - uv0.y);

                        double factor = (wheel > 0) ? 1.1 : 1.0 / 1.1;
                        double newZoom = std::clamp(
                            state_.overlay_.zoom[viewIndex] * factor, 0.1, 50.0);

                        double zOld = state_.overlay_.zoom[viewIndex];
                        state_.overlay_.panU[viewIndex] = cursorU +
                            (state_.overlay_.panU[viewIndex] - cursorU) * (zOld / newZoom);
                        state_.overlay_.panV[viewIndex] = cursorV +
                            (state_.overlay_.panV[viewIndex] - cursorV) * (zOld / newZoom);
                        state_.overlay_.zoom[viewIndex] = newZoom;

                        if (state_.syncZoom_ || state_.syncPan_) {
                            state_.lastSyncSource_ = -1;
                            state_.lastSyncView_ = viewIndex;
                            if (state_.syncZoom_)
                                viewManager_.syncZoom(-1, viewIndex);
                            if (state_.syncPan_)
                                viewManager_.syncPan(-1, viewIndex);
                        }
                    }
                }
            }
        }
    }
    ImGui::EndChild();
    return dirtyMask;
}

bool Interface::drawTagsOnSlice(int viewIndex, const ImVec2& imgPos,
                                const ImVec2& imgSize, const ImVec2& uv0, const ImVec2& uv1,
                                const Volume& vol, const glm::ivec3& currentSlice,
                                int selectedTagIndex) {
    if (!state_.tagsVisible_ || !vol.hasTags()) {
        return false;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 tagColor = IM_COL32(255, 0, 0, 200);

    bool drawn = false;

    int sliceAxis = 0;
    int dimU = 0, dimV = 0;
    if (viewIndex == 0) {
        sliceAxis = 2;
        dimU = 0;
        dimV = 1;
    } else if (viewIndex == 1) {
        sliceAxis = 0;
        dimU = 1;
        dimV = 2;
    } else {
        sliceAxis = 1;
        dimU = 0;
        dimV = 2;
    }

    int currentSlicePos = currentSlice[sliceAxis];

    float uvSpanU = uv1.x - uv0.x;
    float uvSpanV = uv1.y - uv0.y;

    const auto& tagPoints = vol.getTagPoints();
    int tagIdx = 0;
    for (const auto& tagPos : tagPoints) {
        glm::ivec3 voxel;
        vol.transformWorldToVoxel(tagPos, voxel);

        int tagSlicePos = voxel[sliceAxis];
        int sliceDistance = std::abs(tagSlicePos - currentSlicePos);

        if (sliceDistance > 4) {
            ++tagIdx;
            continue;
        }

        int diameterVoxels = (tagIdx == selectedTagIndex) ? 8 : 5;
        if (sliceDistance == 1) {
            diameterVoxels = (tagIdx == selectedTagIndex) ? 6 : 3;
        } else if (sliceDistance >= 2) {
            diameterVoxels = 1;
        }
        ++tagIdx;

        float pixelsPerVoxelU = imgSize.x / static_cast<float>(std::max(vol.dimensions[dimU] - 1, 1));
        float pixelsPerVoxelV = imgSize.y / static_cast<float>(std::max(vol.dimensions[dimV] - 1, 1));
        float avgPixelsPerVoxel = (pixelsPerVoxelU + pixelsPerVoxelV) * 0.5f;
        float circleRadius = (diameterVoxels * avgPixelsPerVoxel * 0.5f) * state_.dpiScale_;

        float normU = static_cast<float>(voxel[dimU]) / static_cast<float>(std::max(vol.dimensions[dimU] - 1, 1));
        float normV = static_cast<float>(voxel[dimV]) / static_cast<float>(std::max(vol.dimensions[dimV] - 1, 1));

        normV = 1.0f - normV;

        float screenX = imgPos.x + (normU - uv0.x) / uvSpanU * imgSize.x;
        float screenY = imgPos.y + (normV - uv0.y) / uvSpanV * imgSize.y;

        ImVec2 clipMin = imgPos;
        ImVec2 clipMax(imgPos.x + imgSize.x, imgPos.y + imgSize.y);

        dl->PushClipRect(clipMin, clipMax, true);
        dl->AddCircle(ImVec2(screenX, screenY), circleRadius, tagColor, 0, 2.0f * state_.dpiScale_);
        dl->PopClipRect();

        drawn = true;
    }

    return drawn;
}

void Interface::switchQCRow(int newRow, GraphicsBackend& backend) {
    if (newRow < 0 || newRow >= qcState_.rowCount())
        return;
    if (newRow == qcState_.currentRowIndex)
        return;

    // Capture per-column display settings before destroying the old volumes
    struct ColumnDisplay {
        ColourMapType colourMap = ColourMapType::GrayScale;
        std::array<double, 2> valueRange = {0.0, 1.0};
        glm::ivec3 sliceIndices{0, 0, 0};
        glm::dvec3 zoom{1.0, 1.0, 1.0};
        glm::dvec3 panU{0.5, 0.5, 0.5};
        glm::dvec3 panV{0.5, 0.5, 0.5};
        int underColourMode = kClampCurrent;
        int overColourMode = kClampCurrent;
        float overlayAlpha = 1.0f;
    };
    std::vector<ColumnDisplay> saved;
    for (int ci = 0; ci < state_.volumeCount(); ++ci)
    {
        const auto& vs = state_.viewStates_[ci];
        saved.push_back({vs.colourMap, vs.valueRange, vs.sliceIndices,
                         vs.zoom, vs.panU, vs.panV,
                         vs.underColourMode, vs.overColourMode, vs.overlayAlpha});
    }

    qcState_.currentRowIndex = newRow;

    // Wait for the GPU to finish before destroying old textures
    backend.waitIdle();
    viewManager_.destroyAllTextures();

    const auto& paths = qcState_.pathsForRow(newRow);
    state_.loadVolumeSet(paths);

    // Restore per-column display settings from previous row
    for (int ci = 0; ci < state_.volumeCount() && ci < static_cast<int>(saved.size()); ++ci)
    {
        if (state_.volumes_[ci].data.empty())
            continue;
        VolumeViewState& vs = state_.viewStates_[ci];
        vs.colourMap = saved[ci].colourMap;
        vs.valueRange = saved[ci].valueRange;
        const Volume& vol = state_.volumes_[ci];
        vs.sliceIndices.x = std::clamp(saved[ci].sliceIndices.x, 0, vol.dimensions.x - 1);
        vs.sliceIndices.y = std::clamp(saved[ci].sliceIndices.y, 0, vol.dimensions.y - 1);
        vs.sliceIndices.z = std::clamp(saved[ci].sliceIndices.z, 0, vol.dimensions.z - 1);
        vs.zoom = saved[ci].zoom;
        vs.panU = saved[ci].panU;
        vs.panV = saved[ci].panV;
        vs.underColourMode = saved[ci].underColourMode;
        vs.overColourMode = saved[ci].overColourMode;
        vs.overlayAlpha = saved[ci].overlayAlpha;
    }

    viewManager_.initializeAllTextures();

    // Rebuild column display names from QC headers
    columnNames_.clear();
    for (int ci = 0; ci < qcState_.columnCount(); ++ci)
        columnNames_.push_back(qcState_.columnNames[ci]);

    scrollToCurrentRow_ = true;

    // Queue adjacent rows for prefetching (loaded on main thread).
    if (prefetcher_)
    {
        std::vector<std::string> prefetchPaths;
        // Collect paths for the previous row
        if (newRow > 0)
        {
            const auto& prev = qcState_.pathsForRow(newRow - 1);
            prefetchPaths.insert(prefetchPaths.end(), prev.begin(), prev.end());
        }
        // Collect paths for the next row
        if (newRow + 1 < qcState_.rowCount())
        {
            const auto& next = qcState_.pathsForRow(newRow + 1);
            prefetchPaths.insert(prefetchPaths.end(), next.begin(), next.end());
        }
        if (!prefetchPaths.empty())
        {
            // OS hint: warm page cache for adjacent rows' files
            os_prefetch_files(prefetchPaths);

            prefetcher_->requestPrefetch(prefetchPaths);
        }
    }
}

void Interface::renderQCVerdictPanel(int volumeIndex) {
    if (qcState_.currentRowIndex < 0
        || qcState_.currentRowIndex >= qcState_.rowCount()
        || volumeIndex >= qcState_.columnCount())
        return;

    auto& result = qcState_.results[qcState_.currentRowIndex];
    auto& verdict = result.verdicts[volumeIndex];
    auto& comment = result.comments[volumeIndex];

    ImGui::PushID(volumeIndex + 5000);

    bool changed = false;

    int vInt = static_cast<int>(verdict);
    if (ImGui::RadioButton("PASS", &vInt, static_cast<int>(QCVerdict::PASS)))
    {
        verdict = QCVerdict::PASS;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("FAIL", &vInt, static_cast<int>(QCVerdict::FAIL)))
    {
        verdict = QCVerdict::FAIL;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("---", &vInt, static_cast<int>(QCVerdict::UNRATED)))
    {
        verdict = QCVerdict::UNRATED;
        changed = true;
    }

    // Comment input
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", comment.c_str());
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputText("##comment", buf, sizeof(buf)))
    {
        comment = buf;
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
        changed = true;

    if (changed && autosave_)
        qcState_.saveOutputCsv();

    ImGui::PopID();
}

void Interface::updateTagFileDialogEntries() {
    namespace fs = std::filesystem;
    tagFileDialogEntries_.clear();

    try {
        fs::path p(tagFileDialogCurrentPath_);
        if (!fs::exists(p) || !fs::is_directory(p))
            p = ".";

        for (const auto& entry : fs::directory_iterator(p)) {
            std::string name = entry.path().filename().string();
            if (entry.is_directory()) {
                name += "/";
            } else {
                std::string ext = entry.path().extension().string();
                if (ext == ".tag" || ext == ".txt" || ext.empty())
                    tagFileDialogEntries_.push_back(name);
            }
        }
        std::sort(tagFileDialogEntries_.begin(), tagFileDialogEntries_.end());
    } catch (...) {
    }
}

void Interface::renderTagFileDialog() {
    if (!tagFileDialogOpen_)
        return;

    ImGui::SetNextWindowSize(ImVec2(500.0f * state_.dpiScale_, 400.0f * state_.dpiScale_));
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    std::string title = tagFileDialogIsSave_ ? "Save Tag File" : "Load Tag File";
    if (ImGui::Begin(title.c_str(), &tagFileDialogOpen_)) {
        float w = ImGui::GetContentRegionAvail().x;
        float btnH = 20.0f * state_.dpiScale_;

        // Current path display and navigation
        ImGui::Text("%s", tagFileDialogCurrentPath_.c_str());
        ImVec2 pathAvail = ImGui::GetContentRegionAvail();

        // Parent directory button
        if (ImGui::Button("..", ImVec2(40, btnH))) {
            std::filesystem::path p(tagFileDialogCurrentPath_);
            if (p.has_parent_path())
                tagFileDialogCurrentPath_ = p.parent_path().string();
            updateTagFileDialogEntries();
        }
        ImGui::SameLine();

        // Path input
        ImGui::SetNextItemWidth(pathAvail.x - 50.0f * state_.dpiScale_);
        char pathBuf[512];
        std::snprintf(pathBuf, sizeof(pathBuf), "%s", tagFileDialogCurrentPath_.c_str());
        if (ImGui::InputText("##dialogpath", pathBuf, sizeof(pathBuf))) {
            tagFileDialogCurrentPath_ = pathBuf;
            updateTagFileDialogEntries();
        }

        ImGui::Separator();

        // File list
        if (ImGui::BeginChild("##filelist", ImVec2(0, -btnH * 3), true)) {
            for (const auto& name : tagFileDialogEntries_) {
                bool isDir = !name.empty() && name.back() == '/';
                if (ImGui::Selectable(name.c_str(), false, isDir ? ImGuiSelectableFlags_DontClosePopups : 0)) {
                    if (isDir) {
                        tagFileDialogCurrentPath_ = std::filesystem::path(tagFileDialogCurrentPath_) / name.substr(0, name.size() - 1);
                        updateTagFileDialogEntries();
                    } else {
                        if (tagFileDialogIsSave_) {
                            tagFileDialogFilename_ = name;
                        } else {
                            std::string fullPath = std::filesystem::path(tagFileDialogCurrentPath_) / name;
                            std::snprintf(state_.combinedTagPath_, sizeof(state_.combinedTagPath_), "%s", fullPath.c_str());
                            if (state_.loadCombinedTags(fullPath)) {
                                state_.recomputeTransform();
                                for (int v = 0; v < state_.volumeCount(); ++v)
                                    for (int vv = 0; vv < 3; ++vv)
                                        viewManager_.updateSliceTexture(v, vv);
                                if (state_.hasOverlay())
                                    viewManager_.updateAllOverlayTextures();
                            }
                            tagFileDialogOpen_ = false;
                        }
                    }
                }
            }
        }
        ImGui::EndChild();

        // Filename input (for save mode)
        if (tagFileDialogIsSave_) {
            ImGui::SetNextItemWidth(w - 100.0f * state_.dpiScale_);
            char nameBuf[256];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", tagFileDialogFilename_.c_str());
            if (ImGui::InputText("##filename", nameBuf, sizeof(nameBuf))) {
                tagFileDialogFilename_ = nameBuf;
            }
            ImGui::SameLine();
            if (ImGui::Button("Save", ImVec2(80, btnH))) {
                if (!tagFileDialogFilename_.empty()) {
                    std::string fullPath = std::filesystem::path(tagFileDialogCurrentPath_) / tagFileDialogFilename_;
                    std::snprintf(state_.combinedTagPath_, sizeof(state_.combinedTagPath_), "%s", fullPath.c_str());
                    state_.saveTags();
                    tagFileDialogOpen_ = false;
                }
            }
        } else {
            if (ImGui::Button("Cancel", ImVec2(w, btnH))) {
                tagFileDialogOpen_ = false;
            }
        }
    }
    ImGui::End();
}

void Interface::updateConfigFileDialogEntries() {
    namespace fs = std::filesystem;
    configFileDialogEntries_.clear();
    fs::path p(configFileDialogCurrentPath_);
    if (!fs::exists(p) || !fs::is_directory(p))
        return;
    for (const auto& entry : fs::directory_iterator(p)) {
        if (entry.is_regular_file()) {
            std::string name = entry.path().filename().string();
            if (name.size() > 5 && name.substr(name.size() - 5) == ".json")
                configFileDialogEntries_.push_back(name);
        }
    }
    std::sort(configFileDialogEntries_.begin(), configFileDialogEntries_.end());
}

void Interface::renderConfigFileDialog() {
    if (!configFileDialogOpen_)
        return;

    std::string title = configFileDialogIsSave_ ? "Save Config File" : "Load Config File";
    if (ImGui::Begin(title.c_str(), &configFileDialogOpen_)) {
        ImGui::Text("%s", configFileDialogCurrentPath_.c_str());

        if (ImGui::Button("..", ImVec2(30, 0))) {
            std::filesystem::path p(configFileDialogCurrentPath_);
            if (p.has_parent_path())
                configFileDialogCurrentPath_ = p.parent_path().string();
            updateConfigFileDialogEntries();
        }

        char pathBuf[512];
        std::snprintf(pathBuf, sizeof(pathBuf), "%s", configFileDialogCurrentPath_.c_str());
        if (ImGui::InputText("Path", pathBuf, sizeof(pathBuf))) {
            configFileDialogCurrentPath_ = pathBuf;
            updateConfigFileDialogEntries();
        }

        ImGui::Separator();
        ImGui::BeginChild("FileList", ImVec2(0, 200));
        for (const auto& name : configFileDialogEntries_) {
            if (ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_::ImGuiSelectableFlags_DontClosePopups)) {
                std::string fullPath = std::filesystem::path(configFileDialogCurrentPath_) / name;
                if (configFileDialogIsSave_) {
                    configFileDialogFilename_ = name;
                } else {
                    configFileDialogOpen_ = false;
                    try {
                        AppConfig cfg = loadConfig(fullPath);
                        int winW, winH;
                        glfwGetWindowSize(interfaceWindow_, &winW, &winH);
                        if (qcState_.active) {
                            qcState_.columnConfigs = cfg.qcColumns.value_or(std::map<std::string, QCColumnConfig>{});
                            state_.localConfigPath_ = fullPath;
                            if (qcState_.rowCount() > 0) {
                                const auto& paths = qcState_.pathsForRow(qcState_.currentRowIndex);
                                state_.loadVolumeSet(paths);
                                for (int ci = 0; ci < qcState_.columnCount() && ci < state_.volumeCount(); ++ci) {
                                    auto it = qcState_.columnConfigs.find(qcState_.columnNames[ci]);
                                    if (it != qcState_.columnConfigs.end()) {
                                        VolumeViewState& vs = state_.viewStates_[ci];
                                        auto cmOpt = colourMapByName(it->second.colourMap);
                                        if (cmOpt) vs.colourMap = *cmOpt;
                                        if (it->second.valueMin) vs.valueRange[0] = *it->second.valueMin;
                                        if (it->second.valueMax) vs.valueRange[1] = *it->second.valueMax;
                                    }
                                }
                                viewManager_.initializeAllTextures();
                            }
                        } else {
                            state_.applyConfig(cfg, winW, winH);
                            state_.localConfigPath_ = fullPath;
                            viewManager_.initializeAllTextures();
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "Failed to load config: " << e.what() << "\n";
                    }
                }
            }
        }
        ImGui::EndChild();

        if (configFileDialogIsSave_) {
            char nameBuf[256];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", configFileDialogFilename_.c_str());
            if (ImGui::InputText("Filename", nameBuf, sizeof(nameBuf))) {
                configFileDialogFilename_ = nameBuf;
            }
            if (ImGui::Button("Save", ImVec2(80, 0))) {
                if (!configFileDialogFilename_.empty()) {
                    std::string fullPath = std::filesystem::path(configFileDialogCurrentPath_) / configFileDialogFilename_;
                    try {
                        AppConfig cfg;
                        cfg.global.defaultColourMap = "GrayScale";
                        int winW, winH;
                        glfwGetWindowSize(interfaceWindow_, &winW, &winH);
                        cfg.global.windowWidth = winW;
                        cfg.global.windowHeight = winH;
                        cfg.global.syncCursors = state_.syncCursors_;
                        cfg.global.syncZoom = state_.syncZoom_;
                        cfg.global.syncPan = state_.syncPan_;
                        cfg.global.showOverlay = state_.showOverlay_;
                        cfg.global.showCrosshairs = state_.showCrosshairs_;
                        cfg.global.tagListVisible = state_.tagListWindowVisible_;
                        cfg.global.autoSaveTags = state_.autoSaveTags_;
                        cfg.global.transformType = transformTypeToString(state_.transformType_);
                        cfg.global.viewVisible = state_.viewVisible;
                        cfg.global.fontPath = state_.fontPath_;
                        cfg.global.fontSize = state_.fontSize_;
                        if (qcState_.active) {
                            cfg.qcColumns = qcState_.columnConfigs;
                        } else {
                            int numVolumes = state_.volumeCount();
                            for (int vi = 0; vi < numVolumes; ++vi) {
                                const VolumeViewState& st = state_.viewStates_[vi];
                                VolumeConfig vc;
                                vc.path = state_.volumePaths_[vi];
                                vc.colourMap = std::string(colourMapName(st.colourMap));
                                vc.valueMin = st.valueRange[0];
                                vc.valueMax = st.valueRange[1];
                                vc.sliceIndices = {st.sliceIndices.x, st.sliceIndices.y, st.sliceIndices.z};
                                vc.zoom = {st.zoom[0], st.zoom[1], st.zoom[2]};
                                vc.panU = {st.panU[0], st.panU[1], st.panU[2]};
                                vc.panV = {st.panV[0], st.panV[1], st.panV[2]};
                                cfg.volumes.push_back(std::move(vc));
                            }
                        }
                        saveConfig(cfg, fullPath);
                        state_.localConfigPath_ = fullPath;
                        configFileDialogOpen_ = false;
                    } catch (const std::exception& e) {
                        std::cerr << "Failed to save config: " << e.what() << "\n";
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80, 0))) {
                configFileDialogOpen_ = false;
            }
        } else {
            if (ImGui::Button("Cancel", ImVec2(80, 0))) {
                configFileDialogOpen_ = false;
            }
        }
    }
    ImGui::End();
}
