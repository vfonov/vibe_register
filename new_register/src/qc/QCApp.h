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

    void run();
    void shutdown();

private:
    std::unique_ptr<Backend> backend_;
    GLFWwindow* window_ = nullptr;
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
    void handleKeyboard(int key, int scancode, int action, int mods);

    static void glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void glfwWindowSizeCallback(GLFWwindow* window, int width, int height);
    static void glfwCloseCallback(GLFWwindow* window);
};

} // namespace QC

#endif // QC_APP_H
