#pragma once

#include "GraphicsBackend.h"

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <backends/imgui_impl_vulkan.h>

/// Vulkan implementation of the GraphicsBackend interface.
/// Owns all Vulkan handles (instance, device, pools, swapchain window data)
/// and manages their full lifecycle.
class VulkanBackend : public GraphicsBackend
{
public:
    VulkanBackend() = default;
    ~VulkanBackend() override = default;

    // --- GraphicsBackend interface ---
    void initialize(GLFWwindow* window) override;
    void shutdown() override;
    void waitIdle() override;

    bool needsSwapchainRebuild() const override;
    void rebuildSwapchain(int width, int height) override;

    void beginFrame() override;
    void endFrame() override;

    void initImGui(GLFWwindow* window) override;
    void shutdownImGui() override;
    void imguiNewFrame() override;
    void imguiRenderDrawData() override;
    void imguiUpdatePlatformWindows() override;

    float contentScale() const override;

private:
    // --- Vulkan handles ---
    VkAllocationCallbacks*   allocator_       = nullptr;
    VkInstance               instance_        = VK_NULL_HANDLE;
    VkPhysicalDevice         physicalDevice_  = VK_NULL_HANDLE;
    VkDevice                 device_          = VK_NULL_HANDLE;
    uint32_t                 queueFamily_     = static_cast<uint32_t>(-1);
    VkQueue                  queue_           = VK_NULL_HANDLE;
    VkDebugReportCallbackEXT debugReport_     = VK_NULL_HANDLE;
    VkPipelineCache          pipelineCache_   = VK_NULL_HANDLE;
    VkDescriptorPool         descriptorPool_  = VK_NULL_HANDLE;
    VkCommandPool            commandPool_     = VK_NULL_HANDLE;
    VkSurfaceKHR             surface_         = VK_NULL_HANDLE;

    ImGui_ImplVulkanH_Window windowData_      = {};
    int                      minImageCount_   = 2;
    bool                     swapChainRebuild_= false;
    float                    contentScale_    = 1.0f;
    GLFWwindow*              window_          = nullptr;

    // --- Private helpers ---
    void createInstance(const char** extensions, uint32_t extensionCount);
    void createDevice();
    void createSwapchainWindow(int width, int height);

    void frameRender(ImDrawData* drawData);
    void framePresent();

    static void checkVkResult(VkResult err);
};
