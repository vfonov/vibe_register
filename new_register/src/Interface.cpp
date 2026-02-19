#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <imgui.h>
#include <imgui_internal.h>

#include "Interface.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>

#include <GLFW/glfw3.h>

#include "AppConfig.h"
#include "ColourMap.h"
#include "QCState.h"
#include "ViewManager.h"

Interface::Interface(AppState& state, ViewManager& viewManager, QCState& qcState)
    : state_(state), viewManager_(viewManager), qcState_(qcState) {}

void Interface::render(GraphicsBackend& backend, GLFWwindow* window) {
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

    const float controlsHeightBase = 160.0f * state_.dpiScale_;
    const float controlsHeight = state_.cleanMode_ ? 0.0f : controlsHeightBase;

    ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    if (!state_.layoutInitialized_ && numVolumes > 0) {
        state_.layoutInitialized_ = true;

        ImVec2 vpSize = ImGui::GetMainViewport()->Size;

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
            toolsFraction = 0.25f;
        else if (totalColumns == 2)
            toolsFraction = 0.16f;
        else if (totalColumns == 3)
            toolsFraction = 0.13f;
        else
            toolsFraction = 0.10f;

        if (qcState_.active) {
            // QC mode: slightly wider for the embedded QC list
            toolsFraction += 0.02f;
            ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, toolsFraction,
                &toolsId, &contentId);
            ImGui::DockBuilderDockWindow("Tools", toolsId);
        } else {
            ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, toolsFraction, &toolsId, &contentId);
            // Split the tools column: Tools on top, Tags on bottom
            ImGuiID toolsTopId, tagsId;
            ImGui::DockBuilderSplitNode(toolsId, ImGuiDir_Up, 0.55f, &toolsTopId, &tagsId);
            ImGui::DockBuilderDockWindow("Tools", toolsTopId);
            ImGui::DockBuilderDockWindow("Tags", tagsId);
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
        if (!qcState_.active && ImGui::IsKeyPressed(ImGuiKey_T)) {
            if (state_.volumeCount() > 0) {
                state_.tagListWindowVisible_ = !state_.tagListWindowVisible_;
            }
        }
        if (qcState_.active) {
            if (ImGui::IsKeyPressed(ImGuiKey_RightBracket))
                switchQCRow(qcState_.currentRowIndex + 1, backend);
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket))
                switchQCRow(qcState_.currentRowIndex - 1, backend);
        }
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

    if (!qcState_.active && state_.tagListWindowVisible_ && state_.volumeCount() > 0) {
        renderTagListWindow();
    }

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
    if (mode == kClampTransparent)
        return 0x00000000;

    ColourMapType mapToUse = currentMap;
    if (mode >= 0 && mode < colourMapCount())
        mapToUse = static_cast<ColourMapType>(mode);

    const ColourLut& lut = colourMapLut(mapToUse);
    return isOver ? lut.table[255] : lut.table[0];
}

const char* Interface::clampColourLabel(int mode) {
    if (mode == kClampCurrent)
        return "Current";
    if (mode == kClampTransparent)
        return "Transparent";
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

        if (!qcState_.active) {
            if (ImGui::Checkbox("Tag List##taglist_tl", &state_.tagListWindowVisible_)) {
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(T)");
        }

        if (!qcState_.active) {
            if (ImGui::Button("Save Local", ImVec2(btnWidth, 0))) {
                try {
                    AppConfig cfg;
                    cfg.global.defaultColourMap = "GrayScale";
                    int winW, winH;
                    glfwGetWindowSize(window, &winW, &winH);
                    cfg.global.windowWidth = winW;
                    cfg.global.windowHeight = winH;
                    cfg.global.syncCursors = state_.syncCursors_;
                    cfg.global.syncZoom = state_.syncZoom_;
                    cfg.global.syncPan = state_.syncPan_;
                    cfg.global.showOverlay = state_.showOverlay_;

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

                    std::string savePath = state_.localConfigPath_.empty()
                        ? "config.json" : state_.localConfigPath_;
                    saveConfig(cfg, savePath);
                } catch (const std::exception& e) {
                    std::cerr << "Failed to save local config: " << e.what() << "\n";
                }
            }
        } // end !qcState_.active (Save Local)

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
        float viewRowHeight = viewAreaHeight / 3.0f;

        if (viewRowHeight < 40.0f * state_.dpiScale_)
            viewRowHeight = 40.0f * state_.dpiScale_;

        for (int v = 0; v < 3; ++v) {
            viewDirtyMask |= renderSliceView(vi, v, ImVec2(viewWidth, viewRowHeight));
        }

        for (int v = 0; v < 3; ++v) {
            if (viewDirtyMask & (1 << v)) {
                viewManager_.updateSliceTexture(vi, v);
            }
        }

        if (!state_.cleanMode_) {
            ImGui::BeginChild("##controls", ImVec2(viewWidth, 0), ImGuiChildFlags_Borders);
            {
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
                        ColourMapType::GrayScale,
                        ColourMapType::Red,
                        ColourMapType::Green,
                        ColourMapType::Blue,
                        ColourMapType::Spectral,
                    };
                    constexpr int nQuick = std::size(quickMaps);

                    auto applyColourMap = [&](ColourMapType cmType) {
                        state.colourMap = cmType;
                        viewManager_.updateSliceTexture(vi, 0);
                        viewManager_.updateSliceTexture(vi, 1);
                        viewManager_.updateSliceTexture(vi, 2);
                        if (state_.hasOverlay())
                            viewManager_.updateAllOverlayTextures();
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

                        if (cmType == ColourMapType::Spectral) {
                            const ColourLut& lut = colourMapLut(ColourMapType::Spectral);
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
                        } else {
                            ColourMapRGBA rep = colourMapRepresentative(cmType);
                            ImU32 col = ImGui::ColorConvertFloat4ToU32(
                                ImVec4(rep.r, rep.g, rep.b, 1.0f));
                            dl->AddRectFilled(pMin, pMax, col);
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
                            if (ImGui::Selectable(colourMapName(cmType).data(), selected)) {
                                applyColourMap(cmType);
                            }
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::PopID();
                }
                ImGui::Separator();

                bool changed = false;
                ImGui::PushID(vi);
                {
                    auto clampCombo = [&](const char* tooltip, const char* id,
                                          int& mode, bool isUnder) -> bool {
                        bool ret = false;
                        if (ImGui::BeginCombo(id, clampColourLabel(mode), ImGuiComboFlags_None)) {
                            auto item = [&](const char* label, int value) -> void {
                                if (ImGui::Selectable(label, mode == value)) {
                                    mode = value;
                                    ret = true;
                                }
                            };

                            auto cmItem = [&](ColourMapType cm) -> void {
                                int idx = static_cast<int>(cm);
                                item(colourMapName(cm).data(), idx);
                            };

                            if (isUnder) {
                                cmItem(ColourMapType::NegRed);
                                cmItem(ColourMapType::NegGreen);
                                cmItem(ColourMapType::NegBlue);
                                ImGui::Separator();
                            }

                            cmItem(ColourMapType::Red);
                            cmItem(ColourMapType::Green);
                            cmItem(ColourMapType::Blue);
                            ImGui::Separator();

                            item("Current", kClampCurrent);
                            item("Transparent", kClampTransparent);
                            ImGui::Separator();

                            for (int cm = 0; cm < colourMapCount(); ++cm) {
                                auto cmt = static_cast<ColourMapType>(cm);
                                if (cmt == ColourMapType::Red ||
                                    cmt == ColourMapType::Green ||
                                    cmt == ColourMapType::Blue ||
                                    cmt == ColourMapType::NegRed ||
                                    cmt == ColourMapType::NegGreen ||
                                    cmt == ColourMapType::NegBlue)
                                    continue;
                                cmItem(cmt);
                            }
                            ImGui::EndCombo();
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", tooltip);
                        return ret;
                    };

                    float avail = ImGui::GetContentRegionAvail().x;
                    float spacing = ImGui::GetStyle().ItemSpacing.x;
                    float autoW = ImGui::CalcTextSize("Auto").x +
                                  ImGui::GetStyle().FramePadding.x * 2.0f;
                    float clampW = ImGui::CalcTextSize("Current__").x +
                                   ImGui::GetStyle().FramePadding.x * 2.0f;
                    float inputTotal = avail - autoW - clampW * 2.0f - spacing * 4.0f;
                    float inputW = inputTotal * 0.5f;
                    if (inputW < 30.0f)
                        inputW = 30.0f;

                    ImGui::SetNextItemWidth(clampW);
                    if (clampCombo("Under colour", "##under", state.underColourMode, true))
                        changed = true;
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(inputW);
                    if (ImGui::InputDouble("##min", &state.valueRange[0], 0.0, 0.0, "%.4g"))
                        changed = true;
                    ImGui::SameLine();
                    if (ImGui::Button("Auto")) {
                        state.valueRange[0] = vol.min_value;
                        state.valueRange[1] = vol.max_value;
                        changed = true;
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(inputW);
                    if (ImGui::InputDouble("##max", &state.valueRange[1], 0.0, 0.0, "%.4g"))
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
                }

                if (ImGui::Button("Reset View")) {
                    for (int v = 0; v < 3; ++v) {
                        state.zoom[v] = 1.0f;
                        state.panU[v] = 0.5f;
                        state.panV[v] = 0.5f;
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
        float viewRowHeight = viewAreaHeight / 3.0f;
        if (viewRowHeight < 40.0f * state_.dpiScale_)
            viewRowHeight = 40.0f * state_.dpiScale_;

        int overlayDirtyMask = 0;
        for (int v = 0; v < 3; ++v)
            overlayDirtyMask |= renderOverlayView(v, ImVec2(avail.x, viewRowHeight));

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
                bool alphaChanged = false;
                int numVolumes = state_.volumeCount();

                if (numVolumes == 2) {
                    float a0 = state_.viewStates_[0].overlayAlpha;
                    float a1 = state_.viewStates_[1].overlayAlpha;
                    float blendT = (a0 + a1 > 0.0f) ? a1 / (a0 + a1) : 0.5f;

                    ImGui::Text("%s", state_.volumeNames_[0].c_str());
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(
                        ImGui::GetContentRegionAvail().x
                        - ImGui::CalcTextSize(state_.volumeNames_[1].c_str()).x
                        - ImGui::GetStyle().ItemSpacing.x);
                    if (ImGui::SliderFloat("##blend", &blendT, 0.0f, 1.0f, "%.2f")) {
                        state_.viewStates_[0].overlayAlpha = 1.0f - blendT;
                        state_.viewStates_[1].overlayAlpha = blendT;
                        alphaChanged = true;
                    }
                    ImGui::SameLine();
                    ImGui::Text("%s", state_.volumeNames_[1].c_str());
                } else {
                    for (int vi = 0; vi < numVolumes; ++vi) {
                        ImGui::PushID(vi + 2000);
                        ImGui::Text("%s", state_.volumeNames_[vi].c_str());
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                        if (ImGui::SliderFloat("##alpha", &state_.viewStates_[vi].overlayAlpha, 0.0f, 1.0f, "%.2f"))
                            alphaChanged = true;
                        ImGui::PopID();
                    }
                }

                if (alphaChanged)
                    viewManager_.updateAllOverlayTextures();

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
    {
        int numVolumes = state_.volumeCount();
        if (numVolumes == 0 || static_cast<int>(state_.volumeNames_.size()) < numVolumes) {
            ImGui::Text("No volumes loaded");
            ImGui::End();
            return;
        }

        int maxTags = state_.getMaxTagCount();

        if (maxTags == 0) {
            ImGui::Text("No tags loaded");
        } else {
            float btnWidth = 120.0f * state_.dpiScale_;
            if (ImGui::Button("Delete Selected", ImVec2(btnWidth, 0))) {
                if (state_.selectedTagIndex_ >= 0 && state_.selectedTagIndex_ < maxTags) {
                    for (int vi = 0; vi < numVolumes; ++vi) {
                        if (state_.selectedTagIndex_ < state_.volumes_[vi].getTagCount()) {
                            state_.volumes_[vi].tags.removeTag(state_.selectedTagIndex_);
                            std::filesystem::path tagPath(state_.volumePaths_[vi]);
                            tagPath.replace_extension(".tag");
                            try {
                                state_.volumes_[vi].saveTags(tagPath.string());
                            } catch (const std::exception& e) {
                                std::cerr << "Failed to save tags: " << e.what() << "\n";
                            }
                        }
                    }
                    state_.selectedTagIndex_ = -1;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(btnWidth, 0))) {
                state_.tagListWindowVisible_ = false;
            }

            ImGui::Separator();

            int tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit;
            if (ImGui::BeginTable("##tags_table", 2 + numVolumes, tableFlags)) {
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                for (int vi = 0; vi < numVolumes; ++vi) {
                    ImGui::TableSetupColumn(state_.volumeNames_[vi].c_str(), ImGuiTableColumnFlags_WidthFixed, 120.0f);
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
    ImGui::End();
}

int Interface::renderSliceView(int vi, int viewIndex, const ImVec2& childSize) {
    int dirtyMask = 0;
    char childId[64];
    std::snprintf(childId, sizeof(childId), "##view_%d_%d", vi, viewIndex);

    VolumeViewState& state = state_.viewStates_[vi];
    const Volume& vol = state_.volumes_[vi];

    ImGui::BeginChild(childId, childSize, ImGuiChildFlags_Borders);
    {
        if (state.sliceTextures[viewIndex]) {
            VulkanTexture* tex = state.sliceTextures[viewIndex].get();
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float sliderHeight = 30.0f * state_.dpiScale_;
            avail.y -= sliderHeight;

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

                imgSize = avail;
                if (imgSize.x / imgSize.y > aspect)
                    imgSize.x = imgSize.y * aspect;
                else
                    imgSize.y = imgSize.x / aspect;

                float padX = (avail.x - imgSize.x) * 0.5f;
                if (padX > 0)
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padX);

                imgPos = ImGui::GetCursorScreenPos();

                float zf = state.zoom[viewIndex];
                float halfU = 0.5f / zf;
                float halfV = 0.5f / zf;
                float centerU = state.panU[viewIndex];
                float centerV = state.panV[viewIndex];
                ImVec2 uv0(centerU - halfU, centerV - halfV);
                ImVec2 uv1(centerU + halfU, centerV + halfV);

                ImGui::Image(
                    reinterpret_cast<ImTextureID>(tex->descriptor_set),
                    imgSize, uv0, uv1);

                {
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

                drawTagsOnSlice(viewIndex, imgPos, imgSize, uv0, uv1, vol, state.sliceIndices);

                bool imageHovered = ImGui::IsItemHovered();
                bool shiftHeld = ImGui::GetIO().KeyShift;

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
                        int tagCount = state_.volumes_[0].getTagCount();
                        std::string newLabel = "Point" + std::to_string(tagCount + 1);

                        for (int v = 0; v < state_.volumeCount(); ++v) {
                            Volume& curVol = state_.volumes_[v];
                            glm::ivec3 volCursorPos = state_.viewStates_[v].sliceIndices;
                            glm::dvec3 volWorldPos;
                            curVol.transformVoxelToWorld(volCursorPos, volWorldPos);

                            std::vector<glm::dvec3> newPoints = curVol.getTagPoints();
                            std::vector<std::string> newLabels = curVol.getTagLabels();
                            newPoints.push_back(volWorldPos);
                            newLabels.push_back(newLabel);
                            curVol.tags.setPoints(newPoints);
                            curVol.tags.setLabels(newLabels);

                            std::filesystem::path tagPath(state_.volumePaths_[v]);
                            tagPath.replace_extension(".tag");
                            try {
                                curVol.saveTags(tagPath.string());
                            } catch (const std::exception& e) {
                                std::cerr << "Failed to save tags: " << e.what() << "\n";
                            }
                        }

                        state_.selectedTagIndex_ = tagCount;
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

            if (!state_.cleanMode_) {
                int sliceDim = -1;
                int maxSlice = 0;
                if (viewIndex == 0) {
                    sliceDim = 0;
                    maxSlice = vol.dimensions.z;
                } else if (viewIndex == 1) {
                    sliceDim = 1;
                    maxSlice = vol.dimensions.x;
                } else {
                    sliceDim = 2;
                    maxSlice = vol.dimensions.y;
                }

                ImGui::PushID(vi * 3 + viewIndex);
                {
                    if (ImGui::Button("-")) {
                        int currentSlice = (viewIndex == 0) ? state.sliceIndices.z
                                             : (viewIndex == 1) ? state.sliceIndices.x
                                                                 : state.sliceIndices.y;
                        if (currentSlice > 0) {
                            if (viewIndex == 0)
                                state.sliceIndices.z--;
                            else if (viewIndex == 1)
                                state.sliceIndices.x--;
                            else
                                state.sliceIndices.y--;
                            dirtyMask |= (1 << viewIndex);
                        }
                    }
                    ImGui::SameLine();

                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 20.0f * state_.dpiScale_);
                    int sliceValue = (viewIndex == 0) ? state.sliceIndices.z
                                  : (viewIndex == 1) ? state.sliceIndices.x
                                                      : state.sliceIndices.y;
                    if (ImGui::SliderInt("##slice", &sliceValue, 0, maxSlice - 1, "%d")) {
                        if (viewIndex == 0)
                            state.sliceIndices.z = sliceValue;
                        else if (viewIndex == 1)
                            state.sliceIndices.x = sliceValue;
                        else
                            state.sliceIndices.y = sliceValue;
                        dirtyMask |= (1 << viewIndex);
                        if (state_.syncCursors_) {
                            state_.lastSyncSource_ = vi;
                            state_.lastSyncView_ = viewIndex;
                            state_.cursorSyncDirty_ = true;
                        }
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("+")) {
                        int currentSlice = (viewIndex == 0) ? state.sliceIndices.z
                                             : (viewIndex == 1) ? state.sliceIndices.x
                                                                 : state.sliceIndices.y;
                        if (currentSlice < maxSlice - 1) {
                            if (viewIndex == 0)
                                state.sliceIndices.z++;
                            else if (viewIndex == 1)
                                state.sliceIndices.x++;
                            else
                                state.sliceIndices.y++;
                            dirtyMask |= (1 << viewIndex);
                            if (state_.syncCursors_) {
                                state_.lastSyncSource_ = vi;
                                state_.lastSyncView_ = viewIndex;
                                state_.cursorSyncDirty_ = true;
                            }
                        }
                    }
                }
                ImGui::PopID();
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
            VulkanTexture* tex = state_.overlay_.textures[viewIndex].get();
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float sliderHeight = 30.0f * state_.dpiScale_;
            avail.y -= sliderHeight;

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

                imgSize = avail;
                if (imgSize.x / imgSize.y > aspect)
                    imgSize.x = imgSize.y * aspect;
                else
                    imgSize.y = imgSize.x / aspect;

                float padX = (avail.x - imgSize.x) * 0.5f;
                if (padX > 0)
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padX);

                imgPos = ImGui::GetCursorScreenPos();

                float zf = state_.overlay_.zoom[viewIndex];
                float halfU = 0.5f / zf;
                float halfV = 0.5f / zf;
                float centerU = state_.overlay_.panU[viewIndex];
                float centerV = state_.overlay_.panV[viewIndex];
                ImVec2 uv0(centerU - halfU, centerV - halfV);
                ImVec2 uv1(centerU + halfU, centerV + halfV);

                ImGui::Image(
                    reinterpret_cast<ImTextureID>(tex->descriptor_set),
                    imgSize, uv0, uv1);

                {
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

            if (!state_.cleanMode_) {
                int maxSlice = (viewIndex == 0) ? ref.dimensions.z
                             : (viewIndex == 1) ? ref.dimensions.x
                                                : ref.dimensions.y;

                ImGui::PushID(100 + viewIndex);
                {
                    if (ImGui::Button("-")) {
                        int refSlice = (viewIndex == 0) ? refState.sliceIndices.z
                                            : (viewIndex == 1) ? refState.sliceIndices.x
                                                               : refState.sliceIndices.y;
                        if (refSlice > 0) {
                            int newSlice = refSlice - 1;
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
                    ImGui::SameLine();

                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30.0f * state_.dpiScale_);
                    int sliceVal = (viewIndex == 0) ? refState.sliceIndices.z
                                : (viewIndex == 1) ? refState.sliceIndices.x
                                                   : refState.sliceIndices.y;
                    if (ImGui::SliderInt("##slice", &sliceVal, 0, maxSlice - 1, "Slice %d")) {
                        for (auto& st : state_.viewStates_) {
                            if (viewIndex == 0)
                                st.sliceIndices.z = sliceVal;
                            else if (viewIndex == 1)
                                st.sliceIndices.x = sliceVal;
                            else
                                st.sliceIndices.y = sliceVal;
                        }
                        dirtyMask |= (1 << viewIndex);
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("+")) {
                        int refSlice = (viewIndex == 0) ? refState.sliceIndices.z
                                            : (viewIndex == 1) ? refState.sliceIndices.x
                                                               : refState.sliceIndices.y;
                        if (refSlice < maxSlice - 1) {
                            int newSlice = refSlice + 1;
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
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
    return dirtyMask;
}

bool Interface::drawTagsOnSlice(int viewIndex, const ImVec2& imgPos,
                                const ImVec2& imgSize, const ImVec2& uv0, const ImVec2& uv1,
                                const Volume& vol, const glm::ivec3& currentSlice) {
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
        dimU = 2;
        dimV = 1;
    } else {
        sliceAxis = 1;
        dimU = 0;
        dimV = 2;
    }

    int currentSlicePos = currentSlice[sliceAxis];

    float uvSpanU = uv1.x - uv0.x;
    float uvSpanV = uv1.y - uv0.y;

    const auto& tagPoints = vol.getTagPoints();
    for (const auto& tagPos : tagPoints) {
        glm::ivec3 voxel;
        vol.transformWorldToVoxel(tagPos, voxel);

        int tagSlicePos = voxel[sliceAxis];
        int sliceDistance = std::abs(tagSlicePos - currentSlicePos);

        if (sliceDistance > 4) {
            continue;
        }

        int diameterVoxels = 5;
        if (sliceDistance == 1) {
            diameterVoxels = 3;
        } else if (sliceDistance >= 2) {
            diameterVoxels = 1;
        }

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
        saved.push_back({vs.colourMap, vs.valueRange, vs.zoom, vs.panU, vs.panV,
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

    state_.layoutInitialized_ = false;
    scrollToCurrentRow_ = true;
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
