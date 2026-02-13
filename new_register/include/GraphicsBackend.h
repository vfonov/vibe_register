#pragma once

#include <memory>
#include <cstdint>

struct GLFWwindow;

/// Abstract graphics backend interface.
/// Encapsulates all GPU initialization, swapchain management, frame
/// rendering, and ImGui integration. Concrete implementations exist
/// per graphics API (Vulkan today, Metal in the future on macOS).
class GraphicsBackend
{
public:
    virtual ~GraphicsBackend() = default;

    // --- Lifecycle ---

    /// Set up the graphics API: create instance, select physical device,
    /// create logical device, allocate pools, create swapchain, and
    /// initialize any helper subsystems (e.g. VulkanHelpers).
    /// @param window  The GLFW window (already created with GLFW_NO_API).
    /// @return true on success.
    virtual bool initialize(GLFWwindow* window) = 0;

    /// Tear down all graphics resources in reverse order of creation.
    virtual void shutdown() = 0;

    /// Wait for the GPU to finish all pending work.
    /// Call this before destroying application-owned GPU resources (textures,
    /// buffers) to ensure they are no longer in use by in-flight frames.
    virtual void waitIdle() = 0;

    // --- Frame cycle ---

    /// Returns true if the swapchain must be rebuilt (e.g. after a resize).
    virtual bool needsSwapchainRebuild() const = 0;

    /// Rebuild the swapchain for the given framebuffer dimensions.
    virtual void rebuildSwapchain(int width, int height) = 0;

    /// Start a new frame: acquire next image, begin command buffer, etc.
    /// Called before any ImGui rendering.
    virtual void beginFrame() = 0;

    /// Finish the frame: end render pass, submit command buffer, present.
    /// Called after ImGui::Render().
    virtual void endFrame() = 0;

    // --- ImGui integration ---

    /// Initialize the ImGui platform and renderer backends for this API.
    virtual void initImGui(GLFWwindow* window) = 0;

    /// Shut down the ImGui backends.
    virtual void shutdownImGui() = 0;

    /// Begin a new ImGui frame for this backend (e.g. ImGui_ImplVulkan_NewFrame).
    virtual void imguiNewFrame() = 0;

    /// Render ImGui draw data and present the frame.
    /// Assumes ImGui::Render() has already been called.
    virtual void imguiRenderDrawData() = 0;

    /// Update platform windows for multi-viewport support.
    virtual void imguiUpdatePlatformWindows() = 0;

    // --- DPI / HiDPI ---

    /// Return the content scale factor for the window (1.0 on standard
    /// displays, 2.0 on Retina / HiDPI).  Uses the larger of the X/Y
    /// scale values reported by the windowing system.
    virtual float contentScale() const = 0;

    // --- Factory ---

    /// Create the default backend for the current platform.
    /// Currently always returns a VulkanBackend.
    static std::unique_ptr<GraphicsBackend> createDefault();
};
