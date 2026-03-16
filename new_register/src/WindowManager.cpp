#include "WindowManager.h"
#include "GraphicsBackend.h"

#include <iostream>

// Static user pointer key for GLFW callback
static constexpr char* WINDOW_MANAGER_KEY = const_cast<char*>("WindowManager");

WindowManager::WindowManager()
    : backend_(nullptr)
    , lastFramebufferWidth_(0)
    , lastFramebufferHeight_(0)
    , swapchainRebuildPending_(false)
{
}

WindowManager::~WindowManager()
{
    clearCallback();
}

void WindowManager::setFramebufferCallback(GLFWwindow* window, GraphicsBackend* backend)
{
    backend_ = backend;
    
    // Store pointer to this instance in GLFW window user data
    glfwSetWindowUserPointer(window, this);
    
    // Set the framebuffer resize callback
    glfwSetFramebufferSizeCallback(window, framebufferCallback);
    
    // Initialize last known dimensions
    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    lastFramebufferWidth_ = fbW;
    lastFramebufferHeight_ = fbH;
    swapchainRebuildPending_ = false;
}

void WindowManager::clearCallback()
{
    glfwSetFramebufferSizeCallback(nullptr, nullptr);
    backend_ = nullptr;
}

bool WindowManager::needsSwapchainRebuild() const
{
    return swapchainRebuildPending_;
}

void WindowManager::getFramebufferSize(int& width, int& height) const
{
    width = lastFramebufferWidth_;
    height = lastFramebufferHeight_;
}

// Static GLFW callback - called when framebuffer is resized
void WindowManager::framebufferCallback(GLFWwindow* window, int width, int height)
{
    // Retrieve the WindowManager instance from user data
    auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (manager)
    {
        manager->onFramebufferResize(width, height);
    }
}

// Instance method that processes the resize event
void WindowManager::onFramebufferResize(int width, int height)
{
    // Detect significant size change (more than 1 pixel to avoid noise)
    bool widthChanged = (width != lastFramebufferWidth_);
    bool heightChanged = (height != lastFramebufferHeight_);
    
    if (widthChanged || heightChanged)
    {
        lastFramebufferWidth_ = width;
        lastFramebufferHeight_ = height;
        
        // Mark swapchain for deferred rebuild
        // The actual rebuild happens in the main loop, not here in the callback
        swapchainRebuildPending_ = true;
        
        // Debug output (optional)
        if (backend_ && backend_->contentScale() > 1.001f)
        {
            std::cerr << "[window] Framebuffer resized: " 
                      << width << "x" << height 
                      << " (swapchain rebuild queued)\n";
        }
    }
}
