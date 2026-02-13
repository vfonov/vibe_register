#include <imgui.h>
#include <imgui_internal.h>
#include <vector>
#include <iostream>
#include <string>
#include <algorithm>
#include <memory>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "GraphicsBackend.h"
#include "Volume.h"
#include "VulkanHelpers.h"

// --- Application State ---
std::vector<Volume> g_Volumes;
int g_CurrentVolumeIndex = 0;
VulkanTexture* g_SliceTextures[3] = { nullptr, nullptr, nullptr }; // 0=Transverse(XY), 1=Sagittal(YZ), 2=Coronal(XZ)
int g_SliceIndices[3] = { 0, 0, 0 }; // Current slice index for each view
float g_WindowLevel[2] = { 0.5f, 1.0f }; // Center, Width
bool g_LayoutInitialized = false;

// --- Forward Declarations ---
void ResetViews();

// --- Volume Rendering Helpers ---
void UpdateSliceTexture(int viewIndex)
{
    if (g_Volumes.empty()) return;
    if (g_CurrentVolumeIndex < 0 ||
        g_CurrentVolumeIndex >= static_cast<int>(g_Volumes.size())) return;

    const Volume& vol = g_Volumes[g_CurrentVolumeIndex];
    if (vol.data.empty()) return;

    int w, h;
    std::vector<uint32_t> pixels;

    // Determine slice dimensions based on view
    // View 0: Transverse (XY plane, Z is slice)
    // View 1: Sagittal (YZ plane, X is slice)
    // View 2: Coronal (XZ plane, Y is slice)

    int dimX = vol.dimensions[0];
    int dimY = vol.dimensions[1];
    int dimZ = vol.dimensions[2];

    if (viewIndex == 0)  // Transverse (Z-slice)
    {
        w = dimX; h = dimY;
        int z = g_SliceIndices[0];
        if (z >= dimZ) z = dimZ - 1;

        pixels.resize(w * h);
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                float val = vol.get(x, y, z);
                val = (val - (g_WindowLevel[0] - g_WindowLevel[1] * 0.5f)) / g_WindowLevel[1];
                if (val < 0) val = 0;
                if (val > 1) val = 1;
                uint8_t c = static_cast<uint8_t>(val * 255.0f);
                // Flip vertical: high Y at low screen Y (radiological convention)
                pixels[(h - 1 - y) * w + x] = 0xFF000000 | (c << 16) | (c << 8) | c;
            }
        }
    }
    else if (viewIndex == 1)  // Sagittal (X-slice)
    {
        w = dimY; h = dimZ;
        int x = g_SliceIndices[1];
        if (x >= dimX) x = dimX - 1;

        pixels.resize(w * h);
        for (int z = 0; z < h; ++z)
        {
            for (int y = 0; y < w; ++y)
            {
                float val = vol.get(x, y, z);
                val = (val - (g_WindowLevel[0] - g_WindowLevel[1] * 0.5f)) / g_WindowLevel[1];
                if (val < 0) val = 0;
                if (val > 1) val = 1;
                uint8_t c = static_cast<uint8_t>(val * 255.0f);
                // Flip vertical: high Z at low screen Y
                pixels[(h - 1 - z) * w + y] = 0xFF000000 | (c << 16) | (c << 8) | c;
            }
        }
    }
    else  // Coronal (Y-slice)
    {
        w = dimX; h = dimZ;
        int y = g_SliceIndices[2];
        if (y >= dimY) y = dimY - 1;

        pixels.resize(w * h);
        for (int z = 0; z < h; ++z)
        {
            for (int x = 0; x < w; ++x)
            {
                float val = vol.get(x, y, z);
                val = (val - (g_WindowLevel[0] - g_WindowLevel[1] * 0.5f)) / g_WindowLevel[1];
                if (val < 0) val = 0;
                if (val > 1) val = 1;
                uint8_t c = static_cast<uint8_t>(val * 255.0f);
                // Flip vertical: high Z at low screen Y
                pixels[(h - 1 - z) * w + x] = 0xFF000000 | (c << 16) | (c << 8) | c;
            }
        }
    }

    if (!g_SliceTextures[viewIndex])
    {
        g_SliceTextures[viewIndex] = VulkanHelpers::CreateTexture(w, h, pixels.data());
    }
    else
    {
        // Check if size changed (different volumes might have different dims)
        if (g_SliceTextures[viewIndex]->width != w ||
            g_SliceTextures[viewIndex]->height != h)
        {
            VulkanHelpers::DestroyTexture(g_SliceTextures[viewIndex]);
            g_SliceTextures[viewIndex] = VulkanHelpers::CreateTexture(w, h, pixels.data());
        }
        else
        {
            VulkanHelpers::UpdateTexture(g_SliceTextures[viewIndex], pixels.data());
        }
    }
}

// --- UI Helpers ---
void ResetViews()
{
    if (g_Volumes.empty()) return;

    const Volume& vol = g_Volumes[g_CurrentVolumeIndex];
    if (!vol.data.empty())
    {
        g_SliceIndices[0] = vol.dimensions[2] / 2;
        g_SliceIndices[1] = vol.dimensions[0] / 2;
        g_SliceIndices[2] = vol.dimensions[1] / 2;

        // Auto-windowing
        float range = vol.max_value - vol.min_value;
        g_WindowLevel[0] = vol.min_value + range * 0.5f; // Center
        g_WindowLevel[1] = range; // Width

        UpdateSliceTexture(0);
        UpdateSliceTexture(1);
        UpdateSliceTexture(2);
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

    // Initialize slice views
    if (!g_Volumes.empty())
    {
        ResetViews();
        std::cerr << "Views initialized.\n";
    }

    std::cerr << "Entering main loop...\n";

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

        if (!g_LayoutInitialized)
        {
            g_LayoutInitialized = true;

            // Clear any existing layout
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId,
                                      ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId,
                                          ImGui::GetMainViewport()->Size);

            // Split into top and bottom halves
            ImGuiID topId, bottomId;
            ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Up, 0.5f,
                                        &topId, &bottomId);

            // Split top into left and right
            ImGuiID topLeftId, topRightId;
            ImGui::DockBuilderSplitNode(topId, ImGuiDir_Left, 0.5f,
                                        &topLeftId, &topRightId);

            // Split bottom into left and right
            ImGuiID bottomLeftId, bottomRightId;
            ImGui::DockBuilderSplitNode(bottomId, ImGuiDir_Left, 0.5f,
                                        &bottomLeftId, &bottomRightId);

            // Dock windows into their positions
            ImGui::DockBuilderDockWindow("Transverse (XY)", topLeftId);
            ImGui::DockBuilderDockWindow("Sagittal (YZ)", topRightId);
            ImGui::DockBuilderDockWindow("Coronal (XZ)", bottomLeftId);
            ImGui::DockBuilderDockWindow("Volume Controls", bottomRightId);

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

        // Volume Controls Window
        ImGui::Begin("Volume Controls");
        if (!g_Volumes.empty())
        {
            // Volume Selector
            if (g_Volumes.size() > 1)
            {
                ImGui::Text("Active Volume:");
                for (int i = 0; i < static_cast<int>(g_Volumes.size()); ++i)
                {
                    char label[32];
                    snprintf(label, sizeof(label), "Volume %d", i);
                    if (ImGui::RadioButton(label, g_CurrentVolumeIndex == i))
                    {
                        g_CurrentVolumeIndex = i;
                        UpdateSliceTexture(0);
                        UpdateSliceTexture(1);
                        UpdateSliceTexture(2);
                    }
                    if (i < static_cast<int>(g_Volumes.size()) - 1)
                        ImGui::SameLine();
                }
                ImGui::Separator();
            }

            const Volume& vol = g_Volumes[g_CurrentVolumeIndex];

            ImGui::Text("Dimensions: %d x %d x %d",
                        vol.dimensions[0], vol.dimensions[1], vol.dimensions[2]);
            ImGui::Separator();

            bool changed = false;
            ImGui::Text("Window / Level");
            if (ImGui::DragFloat("Level (Center)", &g_WindowLevel[0],
                                 (vol.max_value - vol.min_value) * 0.005f))
                changed = true;
            if (ImGui::DragFloat("Width", &g_WindowLevel[1],
                                 (vol.max_value - vol.min_value) * 0.005f))
                changed = true;

            if (ImGui::Button("Auto Level"))
            {
                float range = vol.max_value - vol.min_value;
                g_WindowLevel[0] = vol.min_value + range * 0.5f;
                g_WindowLevel[1] = range;
                changed = true;
            }

            if (changed)
            {
                UpdateSliceTexture(0);
                UpdateSliceTexture(1);
                UpdateSliceTexture(2);
            }
        }
        else
        {
            ImGui::Text("No volume loaded.");
        }
        ImGui::End();

        // Viewports
        const char* viewNames[] = { "Transverse (XY)", "Sagittal (YZ)", "Coronal (XZ)" };
        for (int i = 0; i < 3; ++i)
        {
            ImGui::Begin(viewNames[i]);
            if (g_SliceTextures[i] && !g_Volumes.empty())
            {
                const Volume& vol = g_Volumes[g_CurrentVolumeIndex];

                ImVec2 avail = ImGui::GetContentRegionAvail();
                float controlHeight = 40.0f;
                avail.y -= controlHeight;

                float aspect = static_cast<float>(g_SliceTextures[i]->width) /
                               static_cast<float>(g_SliceTextures[i]->height);
                ImVec2 size = avail;
                if (size.x / size.y > aspect)
                    size.x = size.y * aspect;
                else
                    size.y = size.x / aspect;

                ImGui::Image(
                    reinterpret_cast<ImTextureID>(g_SliceTextures[i]->descriptor_set),
                    size);

                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (avail.y - size.y));

                int maxSlice = (i == 0) ? vol.dimensions[2]
                             : (i == 1) ? vol.dimensions[0]
                                        : vol.dimensions[1];

                ImGui::BeginGroup();
                {
                    if (ImGui::Button("-"))
                    {
                        if (g_SliceIndices[i] > 0)
                        {
                            g_SliceIndices[i]--;
                            UpdateSliceTexture(i);
                        }
                    }
                    ImGui::SameLine();

                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30);
                    if (ImGui::SliderInt("##slice", &g_SliceIndices[i],
                                         0, maxSlice - 1, "Slice %d"))
                    {
                        UpdateSliceTexture(i);
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("+"))
                    {
                        if (g_SliceIndices[i] < maxSlice - 1)
                        {
                            g_SliceIndices[i]++;
                            UpdateSliceTexture(i);
                        }
                    }
                }
                ImGui::EndGroup();
            }
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        backend->endFrame();
    }

    // Cleanup
    backend->shutdownImGui();
    backend->shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
