#pragma once

#include <memory>
#include <cstdint>
#include <vector>

#include <imgui.h>  // for ImTextureID

struct GLFWwindow;

/// Backend-agnostic texture handle.
/// The `id` field is an opaque handle whose meaning depends on the backend:
///   - Vulkan: VkDescriptorSet cast to ImTextureID
///   - OpenGL: GLuint texture name cast to ImTextureID
///   - Metal:  MTLTexture* cast to ImTextureID
/// Application code should never interpret `id` — only pass it to ImGui::Image().
struct Texture
{
    ImTextureID id = 0;
    int width  = 0;
    int height = 0;
};

/// Abstract graphics backend interface.
/// Encapsulates all GPU initialization, swapchain management, frame
/// rendering, and ImGui integration. Concrete implementations exist
/// per graphics API (Vulkan, OpenGL 2, Metal in the future on macOS).
class GraphicsBackend
{
public:
    virtual ~GraphicsBackend() = default;

    // --- Lifecycle ---

    /// Set up the graphics API: create instance, select physical device,
    /// create logical device, allocate pools, create swapchain, and
    /// initialize any helper subsystems (e.g. VulkanHelpers).
    /// @param window  The GLFW window (already created).
    /// @throws std::runtime_error on failure.
    virtual void initialize(GLFWwindow* window) = 0;

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

    // --- DPI / HiDPI ---

    /// Return the content scale factor for the window (1.0 on standard
    /// displays, 2.0 on Retina / HiDPI).  Uses the larger of the X/Y
    /// scale values reported by the windowing system.
    virtual float contentScale() const = 0;

    // --- Screenshot ---

    /// Capture the current swapchain image as RGBA pixel data.
    /// @param[out] width   Image width in pixels.
    /// @param[out] height  Image height in pixels.
    /// @return  Pixel data in RGBA8 format (4 bytes per pixel), or empty
    ///          vector on failure.
    virtual std::vector<uint8_t> captureScreenshot(int& width, int& height) = 0;

    // --- Texture management ---

    /// Create a GPU texture from RGBA8 pixel data.
    /// @param w     Texture width in pixels.
    /// @param h     Texture height in pixels.
    /// @param data  Pointer to w*h*4 bytes of RGBA8 pixel data.
    /// @return Opaque texture handle. Caller owns the returned pointer.
    virtual std::unique_ptr<Texture> createTexture(int w, int h, const void* data) = 0;

    /// Update an existing texture with new RGBA8 pixel data (same dimensions).
    /// @param tex   Texture to update (must not be null).
    /// @param data  Pointer to tex->width * tex->height * 4 bytes.
    virtual void updateTexture(Texture* tex, const void* data) = 0;

    /// Destroy GPU resources associated with a texture.
    /// After this call, tex->id is invalid. The Texture object itself is
    /// still owned by the caller (typically via unique_ptr).
    virtual void destroyTexture(Texture* tex) = 0;

    /// Shut down the texture management subsystem.
    /// Called once at application exit, before shutdownImGui()/shutdown().
    virtual void shutdownTextureSystem() = 0;

    // --- Factory ---

    /// Create the default backend for the current platform.
    /// Currently always returns a VulkanBackend.
    static std::unique_ptr<GraphicsBackend> createDefault();
};
