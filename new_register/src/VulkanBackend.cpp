#include "VulkanBackend.h"
#include "VulkanHelpers.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <iostream>
#include <cstdlib>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

std::unique_ptr<GraphicsBackend> GraphicsBackend::createDefault()
{
    return std::make_unique<VulkanBackend>();
}

// ---------------------------------------------------------------------------
// Static helper
// ---------------------------------------------------------------------------

void VulkanBackend::checkVkResult(VkResult err)
{
    if (err == 0) return;
    std::cerr << "[vulkan] Error: VkResult = " << err << "\n";
    if (err < 0) std::abort();
}

// ---------------------------------------------------------------------------
// Instance creation
// ---------------------------------------------------------------------------

void VulkanBackend::createInstance(const char** extensions, uint32_t extensionCount)
{
    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = extensions;

    VkResult err = vkCreateInstance(&createInfo, allocator_, &instance_);
    checkVkResult(err);
}

// ---------------------------------------------------------------------------
// Physical device selection + logical device + pools
// ---------------------------------------------------------------------------

void VulkanBackend::createDevice()
{
    VkResult err;

    // Enumerate physical devices
    uint32_t gpuCount = 0;
    err = vkEnumeratePhysicalDevices(instance_, &gpuCount, nullptr);
    checkVkResult(err);
    if (gpuCount == 0)
    {
        std::cerr << "Error: No Vulkan physical devices found.\n";
        std::abort();
    }

    std::vector<VkPhysicalDevice> gpus(gpuCount);
    err = vkEnumeratePhysicalDevices(instance_, &gpuCount, gpus.data());
    checkVkResult(err);

    // Select GPU and queue family that supports Graphics AND Present
    physicalDevice_ = VK_NULL_HANDLE;
    queueFamily_ = static_cast<uint32_t>(-1);

    for (uint32_t i = 0; i < gpuCount; ++i)
    {
        uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &queueCount, queues.data());

        for (uint32_t j = 0; j < queueCount; ++j)
        {
            if (queues[j].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                VkBool32 presentSupport = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(gpus[i], j, surface_, &presentSupport);
                if (presentSupport == VK_TRUE)
                {
                    physicalDevice_ = gpus[i];
                    queueFamily_ = j;
                    goto found;
                }
            }
        }
    }
found:

    if (physicalDevice_ == VK_NULL_HANDLE)
    {
        std::cerr << "Error: Could not find a GPU with Graphics and Presentation support.\n";
        std::abort();
    }

    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physicalDevice_, &props);
        std::cerr << "Selected GPU: " << props.deviceName << "\n";
        std::cerr << "Selected Queue Family: " << queueFamily_ << "\n";
    }

    // Create logical device
    {
        const float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo = {};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = queueFamily_;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;

        const char* deviceExtensions[] = { "VK_KHR_swapchain" };

        VkDeviceCreateInfo deviceCreateInfo = {};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &queueInfo;
        deviceCreateInfo.enabledExtensionCount = 1;
        deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions;

        err = vkCreateDevice(physicalDevice_, &deviceCreateInfo, allocator_, &device_);
        checkVkResult(err);
        vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
    }

    // Create descriptor pool
    {
        VkDescriptorPoolSize poolSizes[] =
        {
            { VK_DESCRIPTOR_TYPE_SAMPLER,                1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,   1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,   1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1000 }
        };

        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 1000 * IM_ARRAYSIZE(poolSizes);
        poolInfo.poolSizeCount = static_cast<uint32_t>(IM_ARRAYSIZE(poolSizes));
        poolInfo.pPoolSizes = poolSizes;

        err = vkCreateDescriptorPool(device_, &poolInfo, allocator_, &descriptorPool_);
        checkVkResult(err);
    }

    // Create command pool
    {
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueFamily_;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        err = vkCreateCommandPool(device_, &poolInfo, allocator_, &commandPool_);
        checkVkResult(err);
    }
}

// ---------------------------------------------------------------------------
// Swapchain window
// ---------------------------------------------------------------------------

void VulkanBackend::createSwapchainWindow(int width, int height)
{
    windowData_.Surface = surface_;

    // Select surface format
    const VkFormat requestFormats[] = {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8_UNORM,
        VK_FORMAT_R8G8B8_UNORM
    };
    windowData_.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        physicalDevice_, surface_, requestFormats, IM_ARRAYSIZE(requestFormats),
        VK_COLORSPACE_SRGB_NONLINEAR_KHR);

    // Select present mode
#ifdef IMGUI_UNLIMITED_FRAME_RATE
    const VkPresentModeKHR requestModes[] = {
        VK_PRESENT_MODE_MAILBOX_KHR,
        VK_PRESENT_MODE_IMMEDIATE_KHR,
        VK_PRESENT_MODE_FIFO_KHR
    };
#else
    const VkPresentModeKHR requestModes[] = { VK_PRESENT_MODE_FIFO_KHR };
#endif
    windowData_.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
        physicalDevice_, surface_, requestModes, IM_ARRAYSIZE(requestModes));

    ImGui_ImplVulkanH_CreateOrResizeWindow(
        instance_, physicalDevice_, device_, &windowData_,
        queueFamily_, allocator_, width, height,
        minImageCount_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
}

// ---------------------------------------------------------------------------
// Public lifecycle
// ---------------------------------------------------------------------------

bool VulkanBackend::initialize(GLFWwindow* window)
{
    window_ = window;

    // Query content scale for HiDPI support
    {
        float xscale = 1.0f, yscale = 1.0f;
        glfwGetWindowContentScale(window, &xscale, &yscale);
        contentScale_ = (xscale > yscale) ? xscale : yscale;
        if (contentScale_ < 1.0f) contentScale_ = 1.0f;
        std::cerr << "Content scale: " << contentScale_ << "\n";
    }

    // Check Vulkan support
    if (!glfwVulkanSupported())
    {
        std::cerr << "GLFW: Vulkan not supported\n";
        return false;
    }

    // Get required instance extensions from GLFW
    uint32_t extensionCount = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    std::cerr << "Required Vulkan extensions: " << extensionCount << "\n";

    // Create Vulkan instance
    createInstance(extensions, extensionCount);
    std::cerr << "Vulkan instance created.\n";

    // Create window surface
    VkResult err = glfwCreateWindowSurface(instance_, window, allocator_, &surface_);
    checkVkResult(err);
    std::cerr << "Window surface created.\n";

    // Select GPU and create logical device + pools
    createDevice();
    std::cerr << "Vulkan device created.\n";

    // Create swapchain / framebuffers
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    std::cerr << "Framebuffer size: " << w << " x " << h << "\n";
    createSwapchainWindow(w, h);
    std::cerr << "Vulkan swapchain created.\n";

    // Initialize VulkanHelpers for texture management
    VulkanHelpers::Init(device_, physicalDevice_, queueFamily_, queue_,
                        descriptorPool_, commandPool_);
    std::cerr << "VulkanHelpers initialized.\n";

    return true;
}

void VulkanBackend::shutdown()
{
    VkResult err = vkDeviceWaitIdle(device_);
    checkVkResult(err);

    // Destroy swapchain window resources
    ImGui_ImplVulkanH_DestroyWindow(instance_, device_, &windowData_, allocator_);

    // Destroy pools
    vkDestroyCommandPool(device_, commandPool_, allocator_);
    commandPool_ = VK_NULL_HANDLE;

    vkDestroyDescriptorPool(device_, descriptorPool_, allocator_);
    descriptorPool_ = VK_NULL_HANDLE;

    // Destroy device
    vkDestroyDevice(device_, allocator_);
    device_ = VK_NULL_HANDLE;

    // Destroy surface
    if (surface_ != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(instance_, surface_, allocator_);
        surface_ = VK_NULL_HANDLE;
    }

    // Destroy instance
    vkDestroyInstance(instance_, allocator_);
    instance_ = VK_NULL_HANDLE;
}

void VulkanBackend::waitIdle()
{
    if (device_ != VK_NULL_HANDLE)
    {
        VkResult err = vkDeviceWaitIdle(device_);
        checkVkResult(err);
    }
}

// ---------------------------------------------------------------------------
// Swapchain rebuild
// ---------------------------------------------------------------------------

bool VulkanBackend::needsSwapchainRebuild() const
{
    return swapChainRebuild_;
}

void VulkanBackend::rebuildSwapchain(int width, int height)
{
    ImGui_ImplVulkan_SetMinImageCount(minImageCount_);
    ImGui_ImplVulkanH_CreateOrResizeWindow(
        instance_, physicalDevice_, device_, &windowData_,
        queueFamily_, allocator_, width, height,
        minImageCount_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    windowData_.FrameIndex = 0;
    swapChainRebuild_ = false;
}

// ---------------------------------------------------------------------------
// Frame cycle
// ---------------------------------------------------------------------------

void VulkanBackend::beginFrame()
{
    // Nothing to do here for Vulkan — image acquisition happens in frameRender.
    // This hook exists for backends that need explicit begin-frame work.
}

void VulkanBackend::endFrame()
{
    ImDrawData* drawData = ImGui::GetDrawData();
    const bool isMinimized = (drawData->DisplaySize.x <= 0.0f ||
                              drawData->DisplaySize.y <= 0.0f);
    if (!isMinimized)
        frameRender(drawData);

    // Update and render additional platform windows (multi-viewport)
    imguiUpdatePlatformWindows();

    // Present main window
    if (!isMinimized)
        framePresent();
}

void VulkanBackend::frameRender(ImDrawData* drawData)
{
    ImGui_ImplVulkanH_Window* wd = &windowData_;
    VkResult err;

    VkSemaphore imageAcquired  = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore renderComplete = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;

    err = vkAcquireNextImageKHR(device_, wd->Swapchain, UINT64_MAX,
                                imageAcquired, VK_NULL_HANDLE, &wd->FrameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
    {
        swapChainRebuild_ = true;
        return;
    }
    if (err != VK_SUBOPTIMAL_KHR)
        checkVkResult(err);

    ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];

    {
        err = vkWaitForFences(device_, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
        checkVkResult(err);
        err = vkResetFences(device_, 1, &fd->Fence);
        checkVkResult(err);
    }

    {
        err = vkResetCommandPool(device_, fd->CommandPool, 0);
        checkVkResult(err);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        err = vkBeginCommandBuffer(fd->CommandBuffer, &beginInfo);
        checkVkResult(err);
    }

    {
        VkRenderPassBeginInfo rpInfo = {};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass = wd->RenderPass;
        rpInfo.framebuffer = fd->Framebuffer;
        rpInfo.renderArea.extent.width = wd->Width;
        rpInfo.renderArea.extent.height = wd->Height;
        rpInfo.clearValueCount = 1;
        rpInfo.pClearValues = &wd->ClearValue;
        vkCmdBeginRenderPass(fd->CommandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    }

    ImGui_ImplVulkan_RenderDrawData(drawData, fd->CommandBuffer);

    vkCmdEndRenderPass(fd->CommandBuffer);

    {
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAcquired;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &fd->CommandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderComplete;

        err = vkEndCommandBuffer(fd->CommandBuffer);
        checkVkResult(err);
        err = vkQueueSubmit(queue_, 1, &submitInfo, fd->Fence);
        checkVkResult(err);
    }
}

void VulkanBackend::framePresent()
{
    if (swapChainRebuild_) return;

    ImGui_ImplVulkanH_Window* wd = &windowData_;
    VkSemaphore renderComplete = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderComplete;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &wd->Swapchain;
    presentInfo.pImageIndices = &wd->FrameIndex;

    VkResult err = vkQueuePresentKHR(queue_, &presentInfo);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
    {
        swapChainRebuild_ = true;
        return;
    }
    if (err != VK_SUBOPTIMAL_KHR)
        checkVkResult(err);

    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount;
}

// ---------------------------------------------------------------------------
// ImGui integration
// ---------------------------------------------------------------------------

void VulkanBackend::initImGui(GLFWwindow* window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    // Scale the entire ImGui style for HiDPI
    if (contentScale_ > 1.0f)
    {
        ImGui::GetStyle().ScaleAllSizes(contentScale_);
    }

    // Load default font at scaled size (13px is ImGui's default)
    {
        float fontSize = 13.0f * contentScale_;
        ImFontConfig fontCfg;
        fontCfg.SizePixels = fontSize;
        fontCfg.OversampleH = 1;
        fontCfg.OversampleV = 1;
        fontCfg.PixelSnapH = true;
        io.Fonts->AddFontDefault(&fontCfg);
    }

    // Platform backend
    ImGui_ImplGlfw_InitForVulkan(window, true);

    // Renderer backend
    ImGui_ImplVulkanH_Window* wd = &windowData_;

    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.Instance       = instance_;
    initInfo.PhysicalDevice = physicalDevice_;
    initInfo.Device         = device_;
    initInfo.QueueFamily    = queueFamily_;
    initInfo.Queue          = queue_;
    initInfo.PipelineCache  = pipelineCache_;
    initInfo.DescriptorPool = descriptorPool_;
    initInfo.PipelineInfoMain.RenderPass = wd->RenderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.MinImageCount  = minImageCount_;
    initInfo.ImageCount     = wd->ImageCount;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.Allocator      = allocator_;
    initInfo.CheckVkResultFn = checkVkResult;

    ImGui_ImplVulkan_Init(&initInfo);
    std::cerr << "ImGui Vulkan backend initialized.\n";
}

void VulkanBackend::shutdownImGui()
{
    VkResult err = vkDeviceWaitIdle(device_);
    checkVkResult(err);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void VulkanBackend::imguiNewFrame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
}

void VulkanBackend::imguiRenderDrawData()
{
    // Prefer calling endFrame() directly, which handles render + platform
    // windows + present in the correct order. This method exists to satisfy
    // the GraphicsBackend interface for backends that separate these steps.
    endFrame();
}

void VulkanBackend::imguiUpdatePlatformWindows()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        GLFWwindow* backupContext = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backupContext);
    }
}

float VulkanBackend::contentScale() const
{
    return contentScale_;
}
