#pragma once

#include "Backend.h"

#include <memory>
#include <string>

struct GLFWwindow;

/// Metal (native macOS) backend for new_qc.
/// Uses ImGui's imgui_impl_metal renderer together with the native Cocoa
/// path (imgui_impl_osx) driven by `main_macos.mm`. Unlike the Vulkan and
/// OpenGL2 backends, this backend is NOT driven by GLFW — the application
/// window is a native NSWindow/MTKView supplied via setNativeView().
///
/// This is new_qc's own copy of the top-level new_register MetalBackend,
/// adapted to implement new_qc's narrower `Backend` interface (Backend.h)
/// instead of `GraphicsBackend` — mirroring the existing convention in this
/// directory where Backend/BackendFactory/VulkanBackend/OpenGL2Backend are
/// all separate, new_qc-local copies rather than shared with new_register.
///
/// The header is intentionally free of Objective-C / Metal types so that
/// pure-C++ translation units (e.g. BackendFactory.cpp) can include it.
/// All Objective-C state lives in the PIMPL `Impl` defined in MetalBackend.mm.
class MetalBackend : public Backend
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

    // --- Backend interface ---
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
    struct Impl;
    std::unique_ptr<Impl> impl_;

    float contentScale_     = 1.0f;
    float framebufferScale_ = 1.0f;
    bool  manualScale_      = false;
    bool  swapchainRebuild_ = false;
    std::string fontPath_;
    float fontSize_ = 13.0f;
};
