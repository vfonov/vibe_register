#include "OpenGL2Backend.h"
#include "Backend.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl2.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

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
// OpenGL error checking
// ---------------------------------------------------------------------------

static void checkGLError(const char* operation, const char* file, int line)
{
    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
        const char* errStr = "UNKNOWN";
        switch (err)
        {
            case GL_INVALID_ENUM:      errStr = "GL_INVALID_ENUM"; break;
            case GL_INVALID_VALUE:     errStr = "GL_INVALID_VALUE"; break;
            case GL_INVALID_OPERATION: errStr = "GL_INVALID_OPERATION"; break;
            case GL_OUT_OF_MEMORY:     errStr = "GL_OUT_OF_MEMORY"; break;
        }
        std::cerr << "[opengl2] Error in " << operation << " at " << file << ":" << line
                  << ": " << errStr << " (0x" << std::hex << err << std::dec << ")\n";
    }
}

#define GL_CHECK(op) do { op; checkGLError(#op, __FILE__, __LINE__); } while (0)

// ---------------------------------------------------------------------------
// GLFW hints
// ---------------------------------------------------------------------------

void OpenGL2Backend::setWindowHints()
{
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    // GL 1.0 minimum — GLFW treats this as a lower bound; capable systems
    // get GL 2.1+. Avoids failures on SSH/X11 indirect rendering.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 1);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void OpenGL2Backend::initialize(GLFWwindow* window)
{
    window_ = window;

    GL_CHECK(glfwMakeContextCurrent(window));
    GL_CHECK(glfwSwapInterval(1));

    {
        float xscale = 1.0f, yscale = 1.0f;
        glfwGetWindowContentScale(window, &xscale, &yscale);
        contentScale_ = (xscale > yscale) ? xscale : yscale;
        if (contentScale_ < 1.0f) contentScale_ = 1.0f;
    }

    // Compute framebuffer/window ratio to detect Wayland compositor scaling
    {
        int fbW = 1, winW = 1, fbH, winH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glfwGetWindowSize(window, &winW, &winH);
        framebufferScale_ = (winW > 0) ? static_cast<float>(fbW) / winW : 1.0f;
        if (framebufferScale_ < 1.0f) framebufferScale_ = 1.0f;
    }

    glfwGetFramebufferSize(window, &fbWidth_, &fbHeight_);

    if (debugLoggingEnabled())
    {
        const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const char* version  = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        std::cerr << "[opengl2] Initialized: " << (renderer ? renderer : "unknown")
                  << " (" << (version ? version : "unknown") << ")\n";
    }
}

void OpenGL2Backend::shutdown()
{
    // OpenGL context is destroyed when the GLFW window is destroyed.
}

void OpenGL2Backend::waitIdle()
{
    GL_CHECK(glFinish());
}

// ---------------------------------------------------------------------------
// Frame cycle
// ---------------------------------------------------------------------------

bool OpenGL2Backend::needsSwapchainRebuild() const
{
    return false;
}

void OpenGL2Backend::rebuildSwapchain(int width, int height)
{
    fbWidth_ = width;
    fbHeight_ = height;
    GL_CHECK(glViewport(0, 0, width, height));
}

void OpenGL2Backend::beginFrame()
{
    glfwGetFramebufferSize(window_, &fbWidth_, &fbHeight_);
    GL_CHECK(glViewport(0, 0, fbWidth_, fbHeight_));
}

void OpenGL2Backend::endFrame()
{
    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData) return;

    GL_CHECK(glViewport(0, 0,
        static_cast<int>(drawData->DisplaySize.x),
        static_cast<int>(drawData->DisplaySize.y)));
    GL_CHECK(glClearColor(0.1f, 0.1f, 0.1f, 1.0f));
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT));

    ImGui_ImplOpenGL2_RenderDrawData(drawData);

    GL_CHECK(glfwSwapBuffers(window_));
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

    ImGui::StyleColorsDark();

    ImGui::GetStyle().ScaleAllSizes(imguiScale());
    ImGuiStyle& style = ImGui::GetStyle();
    style.SeparatorSize = ImMax(1.0f, style.SeparatorSize);

    {
        float scaledSize = fontSize_ * imguiScale();
        ImFontConfig fontCfg;
        fontCfg.SizePixels = scaledSize;
        bool loaded = false;
        if (!fontPath_.empty())
        {
            loaded = (io.Fonts->AddFontFromFileTTF(fontPath_.c_str(), scaledSize, &fontCfg) != nullptr);
            if (!loaded)
                std::cerr << "[font] Failed to load font: " << fontPath_
                          << ", falling back to ProggyForever\n";
        }
        if (!loaded)
            io.Fonts->AddFontDefaultVector(&fontCfg);
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

void OpenGL2Backend::setContentScale(float scale)
{
    contentScale_ = scale;
    manualScale_  = true;
}

float OpenGL2Backend::imguiScale() const
{
    if (manualScale_) return contentScale_;
    return contentScale_ / framebufferScale_;
}

void OpenGL2Backend::setFontConfig(const std::string& fontPath, float fontSize)
{
    fontPath_ = fontPath;
    fontSize_ = fontSize;
}

// ---------------------------------------------------------------------------
// Screenshot
// ---------------------------------------------------------------------------

std::vector<uint8_t> OpenGL2Backend::captureScreenshot(int& width, int& height)
{
    GL_CHECK(glReadBuffer(GL_FRONT));
    GL_CHECK(glFinish());

    glfwGetFramebufferSize(window_, &width, &height);
    if (width <= 0 || height <= 0)
    {
        GL_CHECK(glReadBuffer(GL_BACK));
        return {};
    }

    std::vector<uint8_t> pixels(width * height * 4);
    GL_CHECK(glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data()));

    GL_CHECK(glReadBuffer(GL_BACK));

    // Flip vertically (OpenGL reads bottom-to-top)
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
    GL_CHECK(glGenTextures(1, &texId));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, texId));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));

    if (texId == 0)
        throw std::runtime_error("OpenGL2Backend: failed to create texture");

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

    GL_CHECK(glBindTexture(GL_TEXTURE_2D, it->second));
    GL_CHECK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex->width, tex->height,
                    GL_RGBA, GL_UNSIGNED_BYTE, data));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
}

void OpenGL2Backend::destroyTexture(Texture* tex)
{
    if (!tex) return;
    auto it = glTextures_.find(tex->id);
    if (it != glTextures_.end())
    {
        GL_CHECK(glDeleteTextures(1, &it->second));
        glTextures_.erase(it);
    }
    tex->id = 0;
}

void OpenGL2Backend::shutdownTextureSystem()
{
    for (auto& pair : glTextures_)
        GL_CHECK(glDeleteTextures(1, &pair.second));
    glTextures_.clear();
}
