#pragma once

#include <GLFW/glfw3.h>

class GraphicsBackend;

/// Manages GLFW window events and coordinates with the graphics backend.
/// Handles framebuffer resize callbacks and communicates changes to the backend
/// for deferred swapchain rebuild.
class WindowManager
{
public:
    WindowManager();
    ~WindowManager();

    /// Register the framebuffer resize callback for the given window.
    /// The callback marks the backend's swapchain for rebuild on next frame.
    void setFramebufferCallback(GLFWwindow* window, GraphicsBackend* backend);

    /// Clear the resize callback (called during shutdown).
    void clearCallback();

    /// Returns true if a resize was detected since last call.
    bool needsSwapchainRebuild() const;

    /// Get the latest framebuffer dimensions.
    void getFramebufferSize(int& width, int& height) const;

private:
    /// Static GLFW callback - forwards to instance method.
    static void framebufferCallback(GLFWwindow* window, int width, int height);

    /// Instance method that processes the resize event.
    void onFramebufferResize(int width, int height);

    GraphicsBackend* backend_;
    int lastFramebufferWidth_;
    int lastFramebufferHeight_;
    bool swapchainRebuildPending_;
};
