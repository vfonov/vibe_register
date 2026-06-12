#pragma once

#include "GraphicsBackend.h"

#include <memory>
#include <string>

struct GLFWwindow;

/// Metal (native macOS) backend.
/// Uses ImGui's imgui_impl_metal renderer together with the native Cocoa
/// path (imgui_impl_osx) driven by `main_macos.mm`.  Unlike the Vulkan and
/// OpenGL2 backends, this backend is NOT driven by GLFW — the application
/// window is a native NSWindow/MTKView supplied via setNativeView().
///
/// The header is intentionally free of Objective-C / Metal types so that
/// pure-C++ translation units (e.g. BackendFactory.cpp) can include it.
/// All Objective-C state lives in the PIMPL `Impl` defined in MetalBackend.mm.
class MetalBackend : public GraphicsBackend
{
public:
    MetalBackend();
    ~MetalBackend() override;

    // --- Metal-specific setup (called by main_macos.mm) ---

    /// Supply the native MTKView (passed as a `void*` holding an `MTKView*`).
    /// Must be called BEFORE initialize() / initImGui().
    void setNativeView(void* mtkView);

    /// Mark the swapchain as needing rebuild (called from the MTKView
    /// delegate's drawableSizeWillChange:).
    void notifyResize(int width, int height);

    // --- GraphicsBackend interface ---
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

    void requestClose() override;
    void windowSize(int& w, int& h) const override;

    std::vector<uint8_t> captureScreenshot(int& width, int& height) override;

    std::unique_ptr<Texture> createTexture(int w, int h, const void* data) override;
    void updateTexture(Texture* tex, const void* data) override;
    void destroyTexture(Texture* tex) override;
    void shutdownTextureSystem() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    float contentScale_     = 1.0f;
    float framebufferScale_ = 1.0f;
    bool  manualScale_      = false;
    bool  swapchainRebuild_ = false;
    std::string fontPath_;
    float fontSize_ = 13.0f;
};
