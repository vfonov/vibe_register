#include "OpenGL2Backend.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl2.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// We need GL headers for texture management and glReadPixels.
#ifdef _WIN32
#include <GL/gl.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#endif

#include <iostream>
#include <stdexcept>
#include <cstring>

// ---------------------------------------------------------------------------
// GLFW hints
// ---------------------------------------------------------------------------

void OpenGL2Backend::setWindowHints()
{
    // Use default GLFW_OPENGL_API (no hint needed — it's the default).
    // Explicitly reset in case a previous backend attempt set GLFW_NO_API.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    // Do NOT request core profile — GL 2.1 uses compatibility profile.
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void OpenGL2Backend::initialize(GLFWwindow* window)
{
    window_ = window;

    // OpenGL requires making the context current before any GL calls.
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // VSync

    // Query content scale for HiDPI
    {
        float xscale = 1.0f, yscale = 1.0f;
        glfwGetWindowContentScale(window, &xscale, &yscale);
        contentScale_ = (xscale > yscale) ? xscale : yscale;
        if (contentScale_ < 1.0f) contentScale_ = 1.0f;
    }

    // Store initial framebuffer size
    glfwGetFramebufferSize(window, &fbWidth_, &fbHeight_);

    std::cerr << "[opengl2] Initialized: " << glGetString(GL_RENDERER)
              << " (" << glGetString(GL_VERSION) << ")\n";
}

void OpenGL2Backend::shutdown()
{
    // OpenGL context is destroyed when the GLFW window is destroyed.
    // Nothing to do here.
}

void OpenGL2Backend::waitIdle()
{
    glFinish();
}

// ---------------------------------------------------------------------------
// Frame cycle
// ---------------------------------------------------------------------------

bool OpenGL2Backend::needsSwapchainRebuild() const
{
    // OpenGL handles resize automatically — the default framebuffer
    // always matches the window. No explicit "swapchain rebuild" needed.
    return false;
}

void OpenGL2Backend::rebuildSwapchain(int width, int height)
{
    fbWidth_ = width;
    fbHeight_ = height;
    glViewport(0, 0, width, height);
}

void OpenGL2Backend::beginFrame()
{
    // Update framebuffer size each frame (handles resize)
    glfwGetFramebufferSize(window_, &fbWidth_, &fbHeight_);
    glViewport(0, 0, fbWidth_, fbHeight_);
}

void OpenGL2Backend::endFrame()
{
    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData) return;

    glViewport(0, 0,
        static_cast<int>(drawData->DisplaySize.x),
        static_cast<int>(drawData->DisplaySize.y));
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL2_RenderDrawData(drawData);

    glfwSwapBuffers(window_);
}

// ---------------------------------------------------------------------------
// ImGui integration
// ---------------------------------------------------------------------------

void OpenGL2Backend::initImGui(GLFWwindow* window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    // Scale the entire ImGui style for HiDPI
    if (contentScale_ > 1.0f)
        ImGui::GetStyle().ScaleAllSizes(contentScale_);

    // Load default font at scaled size
    {
        float fontSize = 13.0f * contentScale_;
        ImFontConfig fontCfg;
        fontCfg.SizePixels = fontSize;
        fontCfg.OversampleH = 1;
        fontCfg.OversampleV = 1;
        fontCfg.PixelSnapH = true;
        io.Fonts->AddFontDefault(&fontCfg);
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();
}

void OpenGL2Backend::shutdownImGui()
{
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void OpenGL2Backend::imguiNewFrame()
{
    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplGlfw_NewFrame();
}

void OpenGL2Backend::imguiRenderDrawData()
{
    endFrame();
}

float OpenGL2Backend::contentScale() const
{
    return contentScale_;
}

// ---------------------------------------------------------------------------
// Screenshot
// ---------------------------------------------------------------------------

std::vector<uint8_t> OpenGL2Backend::captureScreenshot(int& width, int& height)
{
    glFinish();

    glfwGetFramebufferSize(window_, &width, &height);
    if (width <= 0 || height <= 0)
        return {};

    std::vector<uint8_t> pixels(width * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // OpenGL reads bottom-to-top; flip vertically for top-to-bottom RGBA.
    int rowBytes = width * 4;
    std::vector<uint8_t> rowBuf(rowBytes);
    for (int y = 0; y < height / 2; ++y)
    {
        uint8_t* top = pixels.data() + y * rowBytes;
        uint8_t* bot = pixels.data() + (height - 1 - y) * rowBytes;
        std::memcpy(rowBuf.data(), top, rowBytes);
        std::memcpy(top, bot, rowBytes);
        std::memcpy(bot, rowBuf.data(), rowBytes);
    }

    return pixels;
}

// ---------------------------------------------------------------------------
// Texture management
// ---------------------------------------------------------------------------

std::unique_ptr<Texture> OpenGL2Backend::createTexture(int w, int h, const void* data)
{
    GLuint texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    auto tex = std::make_unique<Texture>();
    tex->id     = static_cast<ImTextureID>(static_cast<intptr_t>(texId));
    tex->width  = w;
    tex->height = h;

    glTextures_[tex->id] = texId;
    return tex;
}

void OpenGL2Backend::updateTexture(Texture* tex, const void* data)
{
    if (!tex) return;
    auto it = glTextures_.find(tex->id);
    if (it == glTextures_.end()) return;

    glBindTexture(GL_TEXTURE_2D, it->second);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex->width, tex->height,
                    GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGL2Backend::destroyTexture(Texture* tex)
{
    if (!tex) return;
    auto it = glTextures_.find(tex->id);
    if (it != glTextures_.end())
    {
        glDeleteTextures(1, &it->second);
        glTextures_.erase(it);
    }
    tex->id = 0;
}

void OpenGL2Backend::shutdownTextureSystem()
{
    for (auto& pair : glTextures_)
        glDeleteTextures(1, &pair.second);
    glTextures_.clear();
}
