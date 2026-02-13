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

#include "GraphicsBackend.h"
#include "Volume.h"
#include "VulkanHelpers.h"

// --- Per-volume view state ---
struct VolumeViewState
{
    VulkanTexture* sliceTextures[3] = { nullptr, nullptr, nullptr };
    int sliceIndices[3] = { 0, 0, 0 };  // Current slice for each view
    float windowLevel[2] = { 0.5f, 1.0f };  // Center, Width
};

// --- Application State ---
std::vector<Volume> g_Volumes;
std::vector<VolumeViewState> g_ViewStates;
bool g_LayoutInitialized = false;

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

    int w, h;
    std::vector<uint32_t> pixels;

    int dimX = vol.dimensions[0];
    int dimY = vol.dimensions[1];
    int dimZ = vol.dimensions[2];

    float wlCenter = state.windowLevel[0];
    float wlWidth  = state.windowLevel[1];
    float wlLow    = wlCenter - wlWidth * 0.5f;

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
                float val = vol.get(x, y, z);
                val = (val - wlLow) / wlWidth;
                if (val < 0) val = 0;
                if (val > 1) val = 1;
                uint8_t c = static_cast<uint8_t>(val * 255.0f);
                pixels[(h - 1 - y) * w + x] = 0xFF000000 | (c << 16) | (c << 8) | c;
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
                float val = vol.get(x, y, z);
                val = (val - wlLow) / wlWidth;
                if (val < 0) val = 0;
                if (val > 1) val = 1;
                uint8_t c = static_cast<uint8_t>(val * 255.0f);
                pixels[(h - 1 - z) * w + y] = 0xFF000000 | (c << 16) | (c << 8) | c;
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
                float val = vol.get(x, y, z);
                val = (val - wlLow) / wlWidth;
                if (val < 0) val = 0;
                if (val > 1) val = 1;
                uint8_t c = static_cast<uint8_t>(val * 255.0f);
                pixels[(h - 1 - z) * w + x] = 0xFF000000 | (c << 16) | (c << 8) | c;
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
    GLFWwindow* window = glfwCreateWindow(1280, 720,
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

    // Initialize slice views for all volumes
    if (!g_Volumes.empty())
    {
        ResetViews();
        std::cerr << "Views initialized for " << g_Volumes.size() << " volume(s).\n";
    }

    std::cerr << "Entering main loop...\n";

    // Pre-generate window names for each volume and view
    // Format: "V1 Transverse (XY)", "V1 Sagittal (YZ)", etc.
    const char* viewSuffixes[] = { "Transverse (XY)", "Sagittal (YZ)", "Coronal (XZ)" };
    int numVolumes = static_cast<int>(g_Volumes.size());

    std::vector<std::string> windowNames;   // [vol * 4 + view] for views, [vol * 4 + 3] for controls
    for (int vi = 0; vi < numVolumes; ++vi)
    {
        for (int v = 0; v < 3; ++v)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "V%d %s", vi + 1, viewSuffixes[v]);
            windowNames.push_back(buf);
        }
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "V%d Controls", vi + 1);
            windowNames.push_back(buf);
        }
    }

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

            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId,
                                      ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId,
                                          ImGui::GetMainViewport()->Size);

            // Split dockspace into N columns (one per volume)
            // We split left-to-right, peeling off one column at a time
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
                    float fraction = 1.0f / static_cast<float>(numVolumes - vi);
                    ImGuiID leftId, rightId;
                    ImGui::DockBuilderSplitNode(remaining, ImGuiDir_Left,
                                                fraction, &leftId, &rightId);
                    columnIds[vi] = leftId;
                    remaining = rightId;
                }
                columnIds[numVolumes - 1] = remaining;
            }

            // Within each column, split into 4 rows: 3 views + controls at bottom
            for (int vi = 0; vi < numVolumes; ++vi)
            {
                // Split off controls at the bottom (give it ~15% of height)
                ImGuiID viewsArea, controlsId;
                ImGui::DockBuilderSplitNode(columnIds[vi], ImGuiDir_Down,
                                            0.15f, &controlsId, &viewsArea);

                // Split views area into 3 equal rows
                ImGuiID topId, middleBottomId;
                ImGui::DockBuilderSplitNode(viewsArea, ImGuiDir_Up, 0.333f,
                                            &topId, &middleBottomId);

                ImGuiID middleId, bottomId;
                ImGui::DockBuilderSplitNode(middleBottomId, ImGuiDir_Up, 0.5f,
                                            &middleId, &bottomId);

                // Dock windows: index into windowNames = vi * 4 + {0,1,2,3}
                int base = vi * 4;
                ImGui::DockBuilderDockWindow(windowNames[base + 0].c_str(), topId);
                ImGui::DockBuilderDockWindow(windowNames[base + 1].c_str(), middleId);
                ImGui::DockBuilderDockWindow(windowNames[base + 2].c_str(), bottomId);
                ImGui::DockBuilderDockWindow(windowNames[base + 3].c_str(), controlsId);
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

        // --- Render each volume's windows ---
        for (int vi = 0; vi < numVolumes; ++vi)
        {
            VolumeViewState& state = g_ViewStates[vi];
            const Volume& vol = g_Volumes[vi];
            int base = vi * 4;

            // Controls window for this volume
            ImGui::Begin(windowNames[base + 3].c_str());
            {
                ImGui::Text("Dimensions: %d x %d x %d",
                            vol.dimensions[0], vol.dimensions[1], vol.dimensions[2]);
                ImGui::Separator();

                bool changed = false;
                ImGui::Text("Window / Level");

                // Use unique IDs per volume to avoid ImGui ID conflicts
                ImGui::PushID(vi);

                if (ImGui::DragFloat("Level (Center)", &state.windowLevel[0],
                                     (vol.max_value - vol.min_value) * 0.005f))
                    changed = true;
                if (ImGui::DragFloat("Width", &state.windowLevel[1],
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
            ImGui::End();

            // View windows for this volume
            for (int v = 0; v < 3; ++v)
            {
                ImGui::Begin(windowNames[base + v].c_str());
                if (state.sliceTextures[v])
                {
                    ImVec2 avail = ImGui::GetContentRegionAvail();
                    float controlHeight = 40.0f;
                    avail.y -= controlHeight;

                    float aspect = static_cast<float>(state.sliceTextures[v]->width) /
                                   static_cast<float>(state.sliceTextures[v]->height);
                    ImVec2 size = avail;
                    if (size.x / size.y > aspect)
                        size.x = size.y * aspect;
                    else
                        size.y = size.x / aspect;

                    ImGui::Image(
                        reinterpret_cast<ImTextureID>(state.sliceTextures[v]->descriptor_set),
                        size);

                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (avail.y - size.y));

                    int maxSlice = (v == 0) ? vol.dimensions[2]
                                 : (v == 1) ? vol.dimensions[0]
                                            : vol.dimensions[1];

                    // Push unique ID for slider/buttons per volume+view
                    ImGui::PushID(vi * 3 + v);
                    ImGui::BeginGroup();
                    {
                        if (ImGui::Button("-"))
                        {
                            if (state.sliceIndices[v] > 0)
                            {
                                state.sliceIndices[v]--;
                                UpdateSliceTexture(vi, v);
                            }
                        }
                        ImGui::SameLine();

                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30);
                        if (ImGui::SliderInt("##slice", &state.sliceIndices[v],
                                             0, maxSlice - 1, "Slice %d"))
                        {
                            UpdateSliceTexture(vi, v);
                        }

                        ImGui::SameLine();
                        if (ImGui::Button("+"))
                        {
                            if (state.sliceIndices[v] < maxSlice - 1)
                            {
                                state.sliceIndices[v]++;
                                UpdateSliceTexture(vi, v);
                            }
                        }
                    }
                    ImGui::EndGroup();
                    ImGui::PopID();
                }
                ImGui::End();
            }
        }

        // Rendering
        ImGui::Render();
        backend->endFrame();
    }

    // Cleanup textures
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

    // Cleanup
    backend->shutdownImGui();
    backend->shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
