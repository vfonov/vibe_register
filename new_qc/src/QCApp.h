#ifndef QC_APP_H
#define QC_APP_H

#include <GLFW/glfw3.h>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include "CSVHandler.h"

namespace QC
{

struct ImageData
{
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    int channels = 0;
    GLuint texture = 0;
};

class QCApp
{
public:
    QCApp();
    ~QCApp();

    // Initialize application with input/output CSV files and optional scale override
    bool init(const std::string& inputFile, const std::string& outputFile, const std::optional<float>& scaleFactor);

    // Run main loop
    void run();

    // Cleanup
    void shutdown();

private:
    GLFWwindow* window_ = nullptr;
    CSVHandler csvHandler_;
    ImageData currentImage_;
    std::string outputFile_;
    size_t currentIndex_ = 0;
    bool running_ = false;

    // UI state
    float imageScale_ = 1.0f;
    float currentScale_ = 1.0f;
    bool autoSave_ = true;
    bool scrollToCurrentRow_ = false;

    // Methods
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
    void handleKeyboard(int key, int scancode, int action, int mods);

    // Static GLFW callbacks
    static void glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void glfwWindowSizeCallback(GLFWwindow* window, int width, int height);
    static void glfwCloseCallback(GLFWwindow* window);
};

} // namespace QC

#endif // QC_APP_H
