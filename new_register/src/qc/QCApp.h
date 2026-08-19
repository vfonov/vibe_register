#ifndef QC_APP_H
#define QC_APP_H

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include "CSVHandler.h"
#include "Backend.h"

struct GLFWwindow;

namespace QC
{

struct ImageData
{
    int width = 0;
    int height = 0;
    int channels = 0;
    std::unique_ptr<Texture> texture;
};

class QCApp
{
public:
    QCApp();
    ~QCApp();

    bool init(const std::string& inputFile, const std::string& outputFile,
              const std::optional<float>& scaleFactor,
              BackendType backendType = BackendType::OpenGL2);

    // Non-GLFW init path: the caller (e.g. a Cocoa/Metal entry point) already
    // owns the native window and has constructed+initialized the backend
    // against it. Runs the window-independent parts of init() (CSV load,
    // initial image load, ImGui init) without touching GLFW.
    bool initWithBackend(std::unique_ptr<Backend> backend,
                          const std::string& inputFile, const std::string& outputFile,
                          const std::optional<float>& scaleFactor);

    void run();

    // One iteration of the render loop's body. Called repeatedly by run()'s
    // GLFW loop, or once per callback by a native frame-driven host (e.g. an
    // MTKViewDelegate) that owns its own run loop.
    void renderFrame();

    void shutdown();

    // True once handleKeyboard() (or an equivalent) has requested the app
    // close (Escape key, or the GLFW window-close callback). A frame-driven
    // host without its own run() loop should poll this after renderFrame().
    bool shouldExit() const { return !running_; }

private:
    std::unique_ptr<Backend> backend_;
    GLFWwindow* window_ = nullptr;
    bool usingGlfw_ = false;
    CSVHandler csvHandler_;
    ImageData currentImage_;
    std::string outputFile_;
    size_t currentIndex_ = 0;
    bool running_ = false;

    float imageScale_ = 1.0f;
    float currentScale_ = 1.0f;
    bool autoSave_ = true;
    bool scrollToCurrentRow_ = false;

    void loadImage(const std::string& path);
    void renderUI();
    void renderImage();
    void renderCaseList();
    void navigateTo(size_t index);
    void navigatePrevious();
    void navigateNext();
    void markAsPass();
    void markAsFail();
    void saveProgress();

    // Polls ImGui's per-frame key state (rather than reacting to a GLFW key
    // callback), so the same code drives both the GLFW and the native
    // Cocoa/Metal frame loop, mirroring Interface.cpp's hotkey handling.
    void handleKeyboard();

    static void glfwWindowSizeCallback(GLFWwindow* window, int width, int height);
    static void glfwCloseCallback(GLFWwindow* window);
};

} // namespace QC

#endif // QC_APP_H
