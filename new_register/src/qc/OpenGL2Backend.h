#pragma once

#include "Backend.h"

#include <map>

struct GLFWwindow;

/// OpenGL 2.1 backend.
/// Uses ImGui's imgui_impl_opengl2 renderer.
/// Suitable for legacy systems, software renderers (Mesa llvmpipe), and SSH/X11 forwarding.
class OpenGL2Backend : public Backend
{
public:
    OpenGL2Backend() = default;
    ~OpenGL2Backend() override = default;

    void setWindowHints() override;
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

    float contentScale() const override;
    void setContentScale(float scale) override;
    float imguiScale() const override;
    void setFontConfig(const std::string& fontPath, float fontSize) override;

    std::vector<uint8_t> captureScreenshot(int& width, int& height) override;

    std::unique_ptr<Texture> createTexture(int w, int h, const void* data) override;
    void updateTexture(Texture* tex, const void* data) override;
    void destroyTexture(Texture* tex) override;
    void shutdownTextureSystem() override;

private:
    GLFWwindow* window_ = nullptr;
    float contentScale_     = 1.0f;
    float framebufferScale_ = 1.0f;
    bool  manualScale_      = false;
    int fbWidth_ = 0;
    int fbHeight_ = 0;
    std::string fontPath_;
    float fontSize_ = 13.0f;

    /// Map from ImTextureID to OpenGL texture name (GLuint), for cleanup.
    std::map<ImTextureID, unsigned int> glTextures_;
};
