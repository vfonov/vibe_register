#include <imgui.h>
#include <imgui_internal.h>
#include <vector>
#include <iostream>
#include <string>
#include <algorithm>
#include <memory>
#include <cstdio>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "ColourMap.h"
#include "GraphicsBackend.h"
#include "Volume.h"
#include "VulkanHelpers.h"

// --- Per-volume view state ---
struct VolumeViewState
{
    VulkanTexture* sliceTextures[3] = { nullptr, nullptr, nullptr };
    int sliceIndices[3] = { 0, 0, 0 };  // Current slice for each view
    float windowLevel[2] = { 0.5f, 1.0f };  // Center, Width
    ColourMapType colourMap = ColourMapType::GrayScale;
};

// --- Application State ---
std::vector<Volume> g_Volumes;
std::vector<VolumeViewState> g_ViewStates;
bool g_LayoutInitialized = false;
float g_DpiScale = 1.0f;  // Set after backend initialisation

// --- Forward Declarations ---
void UpdateSliceTexture(int volumeIndex, int viewIndex);
void ResetViews();

// --- Volume Rendering Helpers ---
void UpdateSliceTexture(int volumeIndex, int viewIndex)
{
    if (volumeIndex < 0 ||
        volumeIndex >= static_cast<int>(g_Volumes.size())) return;
    if (volumeIndex >= static_cast<int>(g_ViewStates.size())) return;

    const Volume& vol = g_Volumes[volumeIndex];
    if (vol.data.empty()) return;

    VolumeViewState& state = g_ViewStates[volumeIndex];
    const ColourLut& lut = colourMapLut(state.colourMap);

    int w, h;
    std::vector<uint32_t> pixels;

    int dimX = vol.dimensions[0];
    int dimY = vol.dimensions[1];
    int dimZ = vol.dimensions[2];

    float wlCenter = state.windowLevel[0];
    float wlWidth  = state.windowLevel[1];
    float wlLow    = wlCenter - wlWidth * 0.5f;

    // Lambda: map a raw voxel value through window/level to a LUT colour.
    auto voxelToColour = [&](float val) -> uint32_t
    {
        val = (val - wlLow) / wlWidth;
        if (val < 0.0f) val = 0.0f;
        if (val > 1.0f) val = 1.0f;
        int idx = static_cast<int>(val * 255.0f + 0.5f);
        if (idx > 255) idx = 255;
        return lut.table[idx];
    };

    if (viewIndex == 0)  // Transverse (Z-slice)
    {
        w = dimX; h = dimY;
        int z = state.sliceIndices[0];
        if (z >= dimZ) z = dimZ - 1;

        pixels.resize(w * h);
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                pixels[(h - 1 - y) * w + x] = voxelToColour(vol.get(x, y, z));
            }
        }
    }
    else if (viewIndex == 1)  // Sagittal (X-slice)
    {
        w = dimY; h = dimZ;
        int x = state.sliceIndices[1];
        if (x >= dimX) x = dimX - 1;

        pixels.resize(w * h);
        for (int z = 0; z < h; ++z)
        {
            for (int y = 0; y < w; ++y)
            {
                pixels[(h - 1 - z) * w + y] = voxelToColour(vol.get(x, y, z));
            }
        }
    }
    else  // Coronal (Y-slice)
    {
        w = dimX; h = dimZ;
        int y = state.sliceIndices[2];
        if (y >= dimY) y = dimY - 1;

        pixels.resize(w * h);
        for (int z = 0; z < h; ++z)
        {
            for (int x = 0; x < w; ++x)
            {
                pixels[(h - 1 - z) * w + x] = voxelToColour(vol.get(x, y, z));
            }
        }
    }

    VulkanTexture*& tex = state.sliceTextures[viewIndex];
    if (!tex)
    {
        tex = VulkanHelpers::CreateTexture(w, h, pixels.data());
    }
    else
    {
        if (tex->width != w || tex->height != h)
        {
            VulkanHelpers::DestroyTexture(tex);
            tex = VulkanHelpers::CreateTexture(w, h, pixels.data());
        }
        else
        {
            VulkanHelpers::UpdateTexture(tex, pixels.data());
        }
    }
}

// --- Initialize state for all loaded volumes ---
void ResetViews()
{
    g_ViewStates.resize(g_Volumes.size());

    for (int vi = 0; vi < static_cast<int>(g_Volumes.size()); ++vi)
    {
        const Volume& vol = g_Volumes[vi];
        if (vol.data.empty()) continue;

        VolumeViewState& state = g_ViewStates[vi];

        state.sliceIndices[0] = vol.dimensions[2] / 2;
        state.sliceIndices[1] = vol.dimensions[0] / 2;
        state.sliceIndices[2] = vol.dimensions[1] / 2;

        float range = vol.max_value - vol.min_value;
        state.windowLevel[0] = vol.min_value + range * 0.5f;
        state.windowLevel[1] = range;

        UpdateSliceTexture(vi, 0);
        UpdateSliceTexture(vi, 1);
        UpdateSliceTexture(vi, 2);
    }
}

// --- Render a single slice view within a child region ---
static void RenderSliceView(int vi, int viewIndex, const ImVec2& childSize,
                            const Volume& vol, VolumeViewState& state)
{
    const char* viewLabels[] = { "Transverse (XY)", "Sagittal (YZ)", "Coronal (XZ)" };
    char childId[64];
    std::snprintf(childId, sizeof(childId), "##view_%d_%d", vi, viewIndex);

    ImGui::BeginChild(childId, childSize, ImGuiChildFlags_Borders);
    {
        // Title label
        ImGui::Text("%s", viewLabels[viewIndex]);

        if (state.sliceTextures[viewIndex])
        {
            VulkanTexture* tex = state.sliceTextures[viewIndex];
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float sliderHeight = 30.0f * g_DpiScale;
            avail.y -= sliderHeight;

            if (avail.x > 0 && avail.y > 0)
            {
                // Compute world-space aspect ratio, accounting for
                // non-uniform voxel spacing.
                //   Transverse (0): X horizontal, Y vertical
                //   Sagittal   (1): Y horizontal, Z vertical
                //   Coronal    (2): X horizontal, Z vertical
                int axisU, axisV;
                if (viewIndex == 0)      { axisU = 0; axisV = 1; }
                else if (viewIndex == 1) { axisU = 1; axisV = 2; }
                else                     { axisU = 0; axisV = 2; }

                double pixelAspect = vol.slicePixelAspect(axisU, axisV);
                float aspect = static_cast<float>(tex->width) /
                               static_cast<float>(tex->height) *
                               static_cast<float>(pixelAspect);

                ImVec2 imgSize = avail;
                if (imgSize.x / imgSize.y > aspect)
                    imgSize.x = imgSize.y * aspect;
                else
                    imgSize.y = imgSize.x / aspect;

                // Center the image horizontally
                float padX = (avail.x - imgSize.x) * 0.5f;
                if (padX > 0)
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padX);

                ImGui::Image(
                    reinterpret_cast<ImTextureID>(tex->descriptor_set),
                    imgSize);
            }

            // Slice navigation slider
            int maxSlice = (viewIndex == 0) ? vol.dimensions[2]
                         : (viewIndex == 1) ? vol.dimensions[0]
                                            : vol.dimensions[1];

            ImGui::PushID(vi * 3 + viewIndex);
            {
                if (ImGui::Button("-"))
                {
                    if (state.sliceIndices[viewIndex] > 0)
                    {
                        state.sliceIndices[viewIndex]--;
                        UpdateSliceTexture(vi, viewIndex);
                    }
                }
                ImGui::SameLine();

                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30.0f * g_DpiScale);
                if (ImGui::SliderInt("##slice", &state.sliceIndices[viewIndex],
                                     0, maxSlice - 1, "Slice %d"))
                {
                    UpdateSliceTexture(vi, viewIndex);
                }

                ImGui::SameLine();
                if (ImGui::Button("+"))
                {
                    if (state.sliceIndices[viewIndex] < maxSlice - 1)
                    {
                        state.sliceIndices[viewIndex]++;
                        UpdateSliceTexture(vi, viewIndex);
                    }
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    // Load volumes
    if (argc > 1)
    {
        for (int i = 1; i < argc; ++i)
        {
            std::cerr << "Loading volume " << (i - 1) << ": " << argv[i] << "\n";
            Volume vol;
            if (vol.load(argv[i]))
            {
                g_Volumes.push_back(std::move(vol));
                const auto& v = g_Volumes.back();
                std::cerr << "Volume loaded. Dimensions: "
                          << v.dimensions[0] << " x "
                          << v.dimensions[1] << " x "
                          << v.dimensions[2] << "\n";
            }
        }
    }
    else
    {
        std::cerr << "Generating test data...\n";
        Volume vol;
        vol.generate_test_data();
        g_Volumes.push_back(std::move(vol));
    }

    if (g_Volumes.empty())
    {
        std::cerr << "No volumes loaded.\n";
    }

    // Setup GLFW
    std::cerr << "Starting application...\n";
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    // Query primary monitor content scale and work area for window sizing
    float initScale = 1.0f;
    int monWorkX = 0, monWorkY = 0, monWorkW = 1280, monWorkH = 720;
    {
        float sx = 1.0f, sy = 1.0f;
        GLFWmonitor* primary = glfwGetPrimaryMonitor();
        if (primary)
        {
            glfwGetMonitorContentScale(primary, &sx, &sy);
            glfwGetMonitorWorkarea(primary, &monWorkX, &monWorkY,
                                  &monWorkW, &monWorkH);
        }
        initScale = (sx > sy) ? sx : sy;
        if (initScale < 1.0f) initScale = 1.0f;
    }

    // Size the window based on the number of loaded volumes.
    // Each volume column gets ~400 logical pixels wide; height is 720
    // or 75% of the monitor work area, whichever is smaller.
    int numVols = static_cast<int>(g_Volumes.size());
    if (numVols < 1) numVols = 1;

    constexpr int colWidth  = 200;  // logical pixels per volume column
    constexpr int baseHeight = 480;

    int initW = static_cast<int>(colWidth * numVols * initScale);
    int initH = static_cast<int>(baseHeight * initScale);

    // Clamp to 90% of the monitor work area
    int maxW = static_cast<int>(monWorkW * 0.9f);
    int maxH = static_cast<int>(monWorkH * 0.9f);
    if (initW > maxW) initW = maxW;
    if (initH > maxH) initH = maxH;

    GLFWwindow* window = glfwCreateWindow(initW, initH,
                                          "New Register (ImGui + Vulkan)",
                                          nullptr, nullptr);
    if (!window)
    {
        std::cerr << "Failed to create GLFW window.\n";
        glfwTerminate();
        return 1;
    }

    // Create and initialize graphics backend
    auto backend = GraphicsBackend::createDefault();
    if (!backend->initialize(window))
    {
        std::cerr << "Failed to initialize graphics backend.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    backend->initImGui(window);

    // Store DPI scale for all UI size computations
    g_DpiScale = backend->contentScale();

    // Initialize slice views for all volumes
    if (!g_Volumes.empty())
    {
        ResetViews();
        std::cerr << "Views initialized for " << g_Volumes.size() << " volume(s).\n";
    }

    std::cerr << "Entering main loop...\n";

    int numVolumes = static_cast<int>(g_Volumes.size());

    // Pre-generate window names — one per volume column
    std::vector<std::string> columnNames;
    for (int vi = 0; vi < numVolumes; ++vi)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Volume %d", vi + 1);
        columnNames.push_back(buf);
    }

    // Fixed height for the controls section at the bottom of each column
    const float controlsHeight = 140.0f * g_DpiScale;

    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // Resize swap chain if needed
        if (backend->needsSwapchainRebuild())
        {
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            if (width > 0 && height > 0)
            {
                backend->rebuildSwapchain(width, height);
            }
        }

        // Start ImGui frame
        backend->imguiNewFrame();
        ImGui::NewFrame();

        // DockSpace with default layout
        ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        if (!g_LayoutInitialized && numVolumes > 0)
        {
            g_LayoutInitialized = true;

            ImVec2 vpSize = ImGui::GetMainViewport()->Size;

            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId,
                                      ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, vpSize);

            // Simple layout: split dockspace into N equal columns,
            // one per volume.  Each column is a single ImGui window
            // that internally uses BeginChild to divide into 3 equal
            // view rows + a fixed-height controls section.
            std::vector<ImGuiID> columnIds(numVolumes);
            if (numVolumes == 1)
            {
                columnIds[0] = dockspaceId;
            }
            else
            {
                ImGuiID remaining = dockspaceId;
                for (int vi = 0; vi < numVolumes - 1; ++vi)
                {
                    float fraction = 1.0f /
                        static_cast<float>(numVolumes - vi);
                    ImGuiID leftId, rightId;
                    ImGui::DockBuilderSplitNode(remaining, ImGuiDir_Left,
                                                fraction,
                                                &leftId, &rightId);
                    columnIds[vi] = leftId;
                    remaining = rightId;
                }
                columnIds[numVolumes - 1] = remaining;
            }

            for (int vi = 0; vi < numVolumes; ++vi)
            {
                ImGui::DockBuilderDockWindow(
                    columnNames[vi].c_str(), columnIds[vi]);
            }

            ImGui::DockBuilderFinish(dockspaceId);
        }

        // Main Menu
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Exit"))
                    glfwSetWindowShouldClose(window, true);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // --- Render each volume's column window ---
        for (int vi = 0; vi < numVolumes; ++vi)
        {
            VolumeViewState& state = g_ViewStates[vi];
            const Volume& vol = g_Volumes[vi];

            ImGui::Begin(columnNames[vi].c_str());
            {
                ImVec2 avail = ImGui::GetContentRegionAvail();

                // Divide available height: 3 equal view rows + fixed
                // controls height at the bottom.  The spacing between
                // children is accounted for by ImGui automatically.
                float viewAreaHeight = avail.y - controlsHeight;
                float viewRowHeight  = viewAreaHeight / 3.0f;
                float viewWidth      = avail.x;

                if (viewRowHeight < 40.0f * g_DpiScale)
                    viewRowHeight = 40.0f * g_DpiScale;

                // Three slice views — equal height
                for (int v = 0; v < 3; ++v)
                {
                    RenderSliceView(vi, v,
                                    ImVec2(viewWidth, viewRowHeight),
                                    vol, state);
                }

                // Controls section
                ImGui::BeginChild("##controls", ImVec2(viewWidth, 0),
                                  ImGuiChildFlags_Borders);
                {
                    ImGui::Text("Dimensions: %d x %d x %d",
                                vol.dimensions[0], vol.dimensions[1],
                                vol.dimensions[2]);
                    ImGui::Text("Voxel size: %.3f x %.3f x %.3f mm",
                                vol.step[0], vol.step[1], vol.step[2]);
                    ImGui::Separator();

                    // Colour map selector: quick-access buttons + dropdown
                    {
                        ImGui::PushID(vi + 1000);

                        // Quick-access colour maps shown as colour swatches
                        static const ColourMapType quickMaps[] = {
                            ColourMapType::GrayScale,
                            ColourMapType::Red,
                            ColourMapType::Green,
                            ColourMapType::Blue,
                            ColourMapType::Spectral,
                        };
                        constexpr int nQuick = sizeof(quickMaps) / sizeof(quickMaps[0]);

                        auto applyColourMap = [&](ColourMapType cmType)
                        {
                            state.colourMap = cmType;
                            UpdateSliceTexture(vi, 0);
                            UpdateSliceTexture(vi, 1);
                            UpdateSliceTexture(vi, 2);
                        };

                        const float swatchSize = 24.0f * g_DpiScale;
                        const float borderThickness = 2.0f * g_DpiScale;

                        for (int qi = 0; qi < nQuick; ++qi)
                        {
                            if (qi > 0) ImGui::SameLine();

                            ColourMapType cmType = quickMaps[qi];
                            bool isActive = (state.colourMap == cmType);

                            ImGui::PushID(qi);

                            ImVec2 cursor = ImGui::GetCursorScreenPos();
                            if (ImGui::InvisibleButton("##swatch",
                                    ImVec2(swatchSize, swatchSize)))
                            {
                                applyColourMap(cmType);
                            }

                            ImDrawList* dl = ImGui::GetWindowDrawList();
                            ImVec2 pMin = cursor;
                            ImVec2 pMax(cursor.x + swatchSize,
                                        cursor.y + swatchSize);

                            if (cmType == ColourMapType::Spectral)
                            {
                                // Draw miniature gradient strip
                                const ColourLut& lut =
                                    colourMapLut(ColourMapType::Spectral);
                                int nStrips = static_cast<int>(swatchSize);
                                for (int s = 0; s < nStrips; ++s)
                                {
                                    float t = static_cast<float>(s) /
                                              static_cast<float>(nStrips - 1);
                                    int idx = static_cast<int>(
                                        t * 255.0f + 0.5f);
                                    if (idx > 255) idx = 255;
                                    uint32_t packed = lut.table[idx];
                                    // Convert 0xAABBGGRR to ImGui's ImU32
                                    // (same layout)
                                    float x0 = pMin.x + static_cast<float>(s);
                                    float x1 = x0 + 1.0f;
                                    dl->AddRectFilled(
                                        ImVec2(x0, pMin.y),
                                        ImVec2(x1, pMax.y),
                                        packed);
                                }
                            }
                            else
                            {
                                // Solid colour swatch
                                ColourMapRGBA rep =
                                    colourMapRepresentative(cmType);
                                ImU32 col = ImGui::ColorConvertFloat4ToU32(
                                    ImVec4(rep.r, rep.g, rep.b, 1.0f));
                                dl->AddRectFilled(pMin, pMax, col);
                            }

                            // Border: white for active, dark for inactive
                            if (isActive)
                            {
                                dl->AddRect(
                                    ImVec2(pMin.x - 1, pMin.y - 1),
                                    ImVec2(pMax.x + 1, pMax.y + 1),
                                    IM_COL32(255, 255, 255, 255),
                                    0.0f, 0, borderThickness);
                            }
                            else
                            {
                                dl->AddRect(pMin, pMax,
                                    IM_COL32(80, 80, 80, 255));
                            }

                            // Tooltip with colour map name on hover
                            if (ImGui::IsItemHovered())
                            {
                                ImGui::SetTooltip("%s",
                                    colourMapName(cmType).data());
                            }

                            ImGui::PopID();
                        }

                        // "More..." dropdown for the remaining colour maps
                        ImGui::SameLine();

                        // Check if current map is one not in quickMaps
                        bool currentInQuick = false;
                        for (int qi = 0; qi < nQuick; ++qi)
                        {
                            if (quickMaps[qi] == state.colourMap)
                            {
                                currentInQuick = true;
                                break;
                            }
                        }

                        const char* moreLabel = currentInQuick
                            ? "More..."
                            : colourMapName(state.colourMap).data();

                        if (ImGui::BeginCombo("##more_maps", moreLabel,
                                              ImGuiComboFlags_NoPreview))
                        {
                            for (int cm = 0; cm < colourMapCount(); ++cm)
                            {
                                auto cmType = static_cast<ColourMapType>(cm);

                                // Skip maps already shown as buttons
                                bool isQuick = false;
                                for (int qi = 0; qi < nQuick; ++qi)
                                {
                                    if (quickMaps[qi] == cmType)
                                    {
                                        isQuick = true;
                                        break;
                                    }
                                }
                                if (isQuick) continue;

                                bool selected = (cmType == state.colourMap);
                                if (ImGui::Selectable(
                                        colourMapName(cmType).data(),
                                        selected))
                                {
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
                    ImGui::Text("Window / Level");

                    ImGui::PushID(vi);

                    if (ImGui::DragFloat("Level (Center)",
                                         &state.windowLevel[0],
                                         (vol.max_value - vol.min_value) * 0.005f))
                        changed = true;
                    if (ImGui::DragFloat("Width",
                                         &state.windowLevel[1],
                                         (vol.max_value - vol.min_value) * 0.005f))
                        changed = true;

                    if (ImGui::Button("Auto Level"))
                    {
                        float range = vol.max_value - vol.min_value;
                        state.windowLevel[0] = vol.min_value + range * 0.5f;
                        state.windowLevel[1] = range;
                        changed = true;
                    }

                    ImGui::PopID();

                    if (changed)
                    {
                        UpdateSliceTexture(vi, 0);
                        UpdateSliceTexture(vi, 1);
                        UpdateSliceTexture(vi, 2);
                    }
                }
                ImGui::EndChild();
            }
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        backend->endFrame();
    }

    // Cleanup: wait for GPU, destroy textures (while ImGui is still alive),
    // then shut down ImGui, then shut down the backend.
    backend->waitIdle();

    for (auto& state : g_ViewStates)
    {
        for (int i = 0; i < 3; ++i)
        {
            if (state.sliceTextures[i])
            {
                VulkanHelpers::DestroyTexture(state.sliceTextures[i]);
                state.sliceTextures[i] = nullptr;
            }
        }
    }

    backend->shutdownImGui();
    backend->shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
