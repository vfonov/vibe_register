#pragma once

#include <memory>
#include <cstdint>
#include <vector>
#include <string>
#include <optional>

#include <imgui.h>  // for ImTextureID

struct GLFWwindow;

/// Simple debug logging query (always disabled in new_qc).
inline bool debugLoggingEnabled() { return false; }

/// Available graphics backend types.
enum class BackendType
{
    Vulkan,
    OpenGL2,
};

/// Backend-agnostic texture handle.
/// The `id` field is an opaque handle whose meaning depends on the backend:
///   - Vulkan: VkDescriptorSet cast to ImTextureID
///   - OpenGL: GLuint texture name cast to ImTextureID
/// Application code should never interpret `id` — only pass it to ImGui::Image().
struct Texture
{
    ImTextureID id = 0;
    int width  = 0;
    int height = 0;
};

/// Abstract graphics backend interface.
/// Encapsulates all GPU initialization, frame rendering, and ImGui integration.
class Backend
{
public:
    virtual ~Backend() = default;

    // --- Lifecycle ---

    /// Set GLFW window hints appropriate for this backend.
    /// Must be called BEFORE glfwCreateWindow().
    virtual void setWindowHints() = 0;

    /// Set up the graphics API: create device, pools, swapchain, etc.
    /// @param window  The GLFW window (already created).
    virtual void initialize(GLFWwindow* window) = 0;

    /// Tear down all graphics resources.
    virtual void shutdown() = 0;

    /// Wait for the GPU to finish all pending work.
    virtual void waitIdle() = 0;

    // --- Frame cycle ---

    /// Returns true if the swapchain must be rebuilt (e.g. after a resize).
    virtual bool needsSwapchainRebuild() const = 0;

    /// Rebuild the swapchain for the given framebuffer dimensions.
    virtual void rebuildSwapchain(int width, int height) = 0;

    /// Start a new frame (acquire next image, begin command buffer, etc.).
    virtual void beginFrame() = 0;

    /// Finish the frame (render, present).
    virtual void endFrame() = 0;

    // --- ImGui integration ---

    /// Initialize the ImGui platform and renderer backends for this API.
    virtual void initImGui(GLFWwindow* window) = 0;

    /// Shut down the ImGui backends.
    virtual void shutdownImGui() = 0;

    /// Begin a new ImGui frame for this backend.
    virtual void imguiNewFrame() = 0;

    /// Render ImGui draw data and present the frame.
    virtual void imguiRenderDrawData() = 0;

    // --- DPI / HiDPI ---

    /// Return the content scale factor for the window.
    virtual float contentScale() const = 0;

    /// Override the content scale factor.
    virtual void setContentScale(float scale) = 0;

    /// Set the font configuration to use during initImGui().
    /// Must be called BEFORE initImGui().
    virtual void setFontConfig(const std::string& fontPath, float fontSize) = 0;

    // --- Screenshot ---

    /// Capture the current frame as RGBA pixel data.
    virtual std::vector<uint8_t> captureScreenshot(int& width, int& height) = 0;

    // --- Texture management ---

    /// Create a GPU texture from RGBA8 pixel data.
    virtual std::unique_ptr<Texture> createTexture(int w, int h, const void* data) = 0;

    /// Update an existing texture with new pixel data (same dimensions).
    virtual void updateTexture(Texture* tex, const void* data) = 0;

    /// Destroy GPU resources associated with a texture.
    virtual void destroyTexture(Texture* tex) = 0;

    /// Shut down the texture management subsystem.
    virtual void shutdownTextureSystem() = 0;

    // --- Factory ---

    /// Create a backend of the specified type.
    static std::unique_ptr<Backend> create(BackendType type);

    /// Auto-detect the best available backend for the current platform.
    static BackendType detectBest();

    /// Convert a BackendType to a human-readable string.
    static const char* backendName(BackendType type);

    /// Parse a backend name string (case-insensitive).
    static std::optional<BackendType> parseBackendName(const std::string& name);
};
