#pragma once

#include "GraphicsBackend.h"

#include <map>

struct GLFWwindow;

/// OpenGL 2 (fixed-function pipeline) backend.
/// Uses ImGui's imgui_impl_opengl2 renderer backend.
/// This is the simplest backend, suitable for legacy Linux systems,
/// software renderers (Mesa llvmpipe), and SSH/X11 forwarding.
class OpenGL2Backend : public GraphicsBackend
{
public:
    OpenGL2Backend() = default;
    ~OpenGL2Backend() override = default;

    // --- Lifecycle ---
    void setWindowHints() override;
    void initialize(GLFWwindow* window) override;
    void shutdown() override;
    void waitIdle() override;

    // --- Frame cycle ---
    bool needsSwapchainRebuild() const override;
    void rebuildSwapchain(int width, int height) override;
    void beginFrame() override;
    void endFrame() override;

    // --- ImGui integration ---
    void initImGui(GLFWwindow* window) override;
    void shutdownImGui() override;
    void imguiNewFrame() override;
    void imguiRenderDrawData() override;

    // --- DPI ---
    float contentScale() const override;

    // --- Screenshot ---
    std::vector<uint8_t> captureScreenshot(int& width, int& height) override;

    // --- Texture management ---
    std::unique_ptr<Texture> createTexture(int w, int h, const void* data) override;
    void updateTexture(Texture* tex, const void* data) override;
    void destroyTexture(Texture* tex) override;
    void shutdownTextureSystem() override;

private:
    GLFWwindow* window_ = nullptr;
    float contentScale_ = 1.0f;
    int fbWidth_ = 0;
    int fbHeight_ = 0;

    /// Map from ImTextureID to OpenGL texture name (GLuint), for cleanup.
    std::map<ImTextureID, unsigned int> glTextures_;
};
