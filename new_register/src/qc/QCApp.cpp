#include "QCApp.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "WaylandTouchInput.h"

namespace QC
{

QCApp::QCApp() = default;

QCApp::~QCApp()
{
    shutdown();
}

bool QCApp::init(const std::string& inputFile, const std::string& outputFile,
                 const std::optional<float>& scaleFactor, BackendType backendType)
{
    outputFile_ = outputFile;

    if (!csvHandler_.loadOutputCSV(outputFile))
    {
        if (!csvHandler_.loadInputCSV(inputFile))
            return false;
    }

    if (csvHandler_.getRecordCount() == 0)
    {
        std::cerr << "Error: No records to process" << std::endl;
        return false;
    }

    // Create backend
    try
    {
        backend_ = Backend::create(backendType);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: Failed to create backend '" << Backend::backendName(backendType)
                  << "': " << e.what() << std::endl;
        return false;
    }

    // On Wayland sessions, force GLFW to use its native Wayland backend so that
    // wl_touch events (finger touch on touch screens) are delivered correctly.
    // Without this, GLFW may use XWayland where touch events are silently dropped.
    // glfwInitHint(GLFW_PLATFORM, …) requires GLFW 3.4+.
#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
    if (getenv("WAYLAND_DISPLAY") != nullptr)
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
#endif

    // Initialize GLFW
    if (!glfwInit())
    {
        std::cerr << "Error: Failed to initialize GLFW" << std::endl;
        std::cerr << "Note: new_qc requires a display (X11 or Wayland) to run." << std::endl;
        std::cerr << "If running in a headless environment, use a virtual framebuffer:" << std::endl;
        std::cerr << "  xvfb-run ./new_qc input.csv output.csv" << std::endl;
        return false;
    }

    // Set backend-specific window hints
    backend_->setWindowHints();

    // Always enable GLFW_SCALE_TO_MONITOR for proper HiDPI framebuffer scaling
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    // Determine window size: use scale override or query monitor content scale
    float initScale = 1.0f;
    if (scaleFactor.has_value())
    {
        initScale = scaleFactor.value();
        std::cout << "Using scale override: " << initScale << std::endl;
    }
    else
    {
        float sx = 1.0f, sy = 1.0f;
        GLFWmonitor* primary = glfwGetPrimaryMonitor();
        if (primary)
        {
            glfwGetMonitorContentScale(primary, &sx, &sy);
            std::cout << "Monitor content scale: " << sx << " x " << sy << std::endl;
        }
        initScale = (sx > sy) ? sx : sy;
        if (initScale < 1.0f) initScale = 1.0f;
    }

    int initW = 1280;
    int initH = 720;

    window_ = glfwCreateWindow(initW, initH, "new_qc - Quality Control", nullptr, nullptr);
    if (!window_)
    {
        std::cerr << "Error: Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }

    // Set up application GLFW callbacks BEFORE backend initImGui so ImGui chains them
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, glfwKeyCallback);
    glfwSetWindowSizeCallback(window_, glfwWindowSizeCallback);
    glfwSetWindowCloseCallback(window_, glfwCloseCallback);

    // Initialize the graphics backend
    try
    {
        backend_->initialize(window_);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: Backend initialization failed: " << e.what() << std::endl;
        glfwDestroyWindow(window_);
        window_ = nullptr;
        glfwTerminate();
        return false;
    }

    // Apply scale override to backend if provided
    if (scaleFactor.has_value())
        backend_->setContentScale(scaleFactor.value());

    // On X11 HiDPI (no compositor scaling), resize the window to match the
    // effective ImGui scale (Wayland already rendered at the correct size).
    {
        float scale = backend_->imguiScale();
        if (scale > 1.001f)
        {
            int winW, winH;
            glfwGetWindowSize(window_, &winW, &winH);
            if (winW <= 1280)  // only if GLFW_SCALE_TO_MONITOR didn't already enlarge it
                glfwSetWindowSize(window_, static_cast<int>(1280 * scale),
                                           static_cast<int>(720  * scale));
        }
    }

    currentScale_ = backend_->imguiScale();

    // Initialize ImGui through the backend
    backend_->initImGui(window_);

#ifdef HAS_WAYLAND_TOUCH
    WaylandTouch::install(window_);
#endif

    // Load initial image
    loadImage(csvHandler_.getRecords()[currentIndex_].picture_path);

    running_ = true;
    return true;
}

void QCApp::run()
{
    while (running_)
    {
        glfwPollEvents();

        // Handle swapchain rebuild (Vulkan resize)
        if (backend_->needsSwapchainRebuild())
        {
            int w, h;
            glfwGetFramebufferSize(window_, &w, &h);
            if (w > 0 && h > 0)
                backend_->rebuildSwapchain(w, h);
        }

        backend_->beginFrame();

        backend_->imguiNewFrame();
        ImGui::NewFrame();

        renderUI();

        ImGui::Render();

        backend_->imguiRenderDrawData();
    }
}

void QCApp::shutdown()
{
    if (!backend_) return;

    // Wait for all in-flight GPU work before releasing any resources.
    // The last frame was just submitted; its command buffer still references
    // the current texture's descriptor set. Destroying the texture before the
    // GPU is idle causes VK_ERROR_DEVICE_LOST on the next vkDeviceWaitIdle call.
    backend_->waitIdle();

    if (currentImage_.texture)
    {
        backend_->destroyTexture(currentImage_.texture.get());
        currentImage_.texture.reset();
    }
    backend_->shutdownTextureSystem();
    backend_->shutdownImGui();

    // backend_->shutdown() must come BEFORE glfwDestroyWindow():
    // shutdown() destroys the VkSwapchainKHR and VkSurfaceKHR, which reference
    // the OS window handle. Destroying the GLFW window first invalidates that
    // handle, causing vkDeviceWaitIdle (called inside shutdown) to return
    // VK_ERROR_DEVICE_LOST on most drivers.
    backend_->shutdown();
    backend_.reset();

#ifdef HAS_WAYLAND_TOUCH
    WaylandTouch::shutdown();
#endif

    if (window_)
    {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }

    glfwTerminate();
}

void QCApp::loadImage(const std::string& path)
{
    // Destroy previous texture — wait for GPU to finish all in-flight frames first.
    // Without this, the Vulkan descriptor set freed by destroyTexture() may still be
    // referenced by a submitted (but not yet executed) command buffer, causing
    // vkQueueWaitIdle to fail with device-lost in the next UpdateTexture() call.
    if (currentImage_.texture)
    {
        backend_->waitIdle();
        backend_->destroyTexture(currentImage_.texture.get());
        currentImage_.texture.reset();
    }

    int width, height, channels;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);

    if (!data)
    {
        std::cerr << "Warning: Failed to load image: " << path << std::endl;
        currentImage_.width = 0;
        currentImage_.height = 0;
        return;
    }

    currentImage_.width = width;
    currentImage_.height = height;
    currentImage_.channels = channels;

    currentImage_.texture = backend_->createTexture(width, height, data);
    stbi_image_free(data);

    std::cout << "Loaded image: " << path << " (" << width << "x" << height << ")" << std::endl;
}

void QCApp::renderCaseList()
{
    auto& records = csvHandler_.getRecords();
    int totalTableCols = 3;
    int tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
                   | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX;

    if (ImGui::BeginTable("##qc_list", totalTableCols, tableFlags))
    {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Visit", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (size_t ri = 0; ri < records.size(); ++ri)
        {
            ImGui::TableNextRow();

            const auto& record = records[ri];
            bool isCurrent = (ri == currentIndex_);

            if (record.qc_status == "Fail")
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(180, 40, 40, 60));
            else if (record.qc_status == "Pass")
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(40, 180, 40, 60));

            ImGui::TableSetColumnIndex(0);

            char selectId[64];
            std::snprintf(selectId, sizeof(selectId), "##qc_%zu", ri);
            ImGuiSelectableFlags selFlags = ImGuiSelectableFlags_SpanAllColumns
                                          | ImGuiSelectableFlags_AllowOverlap;
            if (ImGui::Selectable(selectId, isCurrent, selFlags))
                navigateTo(ri);

            if (isCurrent && scrollToCurrentRow_)
            {
                ImGui::SetScrollHereY();
                scrollToCurrentRow_ = false;
            }

            ImGui::SameLine();
            ImGui::Text("%zu", ri);

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", record.id.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", record.visit.c_str());
        }

        ImGui::EndTable();
    }
}

void QCApp::renderUI()
{
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("##Main Window", nullptr, windowFlags);

    float leftColumnWidth = ImGui::CalcTextSize("Notes").x + 400.0f;
    leftColumnWidth *= currentScale_;
    float rightColumnWidth = ImGui::GetContentRegionAvail().x - leftColumnWidth;

    ImGui::BeginChild("LeftColumn", ImVec2(leftColumnWidth, 0), true);

    auto& records = csvHandler_.getRecords();
    auto& currentRecord = records[currentIndex_];

    float navBtnWidth = ImGui::CalcTextSize("Previous").x + 30.0f;
    float saveBtnWidth = ImGui::CalcTextSize("Save Results").x + 30.0f;
    bool atFirst = (currentIndex_ <= 0);
    bool atLast = (currentIndex_ >= records.size() - 1);

    if (atFirst) ImGui::BeginDisabled();
    if (ImGui::Button("Previous", ImVec2(navBtnWidth * currentScale_, 0)))
        navigatePrevious();
    if (atFirst) ImGui::EndDisabled();

    ImGui::SameLine();

    if (atLast) ImGui::BeginDisabled();
    if (ImGui::Button("Next", ImVec2(navBtnWidth * currentScale_, 0)))
        navigateNext();
    if (atLast) ImGui::EndDisabled();

    ImGui::Checkbox("Autosave results", &autoSave_);
    if (ImGui::Button("Save Results", ImVec2(saveBtnWidth * currentScale_, 0)))
        saveProgress();

    ImGui::Separator();
    ImGui::Text("ID: %s", currentRecord.id.c_str());
    ImGui::Separator();
    ImGui::Text("Visit: %s", currentRecord.visit.c_str());
    ImGui::Separator();
    ImGui::Text("Image: %s", currentRecord.picture_path.c_str());
    ImGui::Separator();

    ImGui::Separator();
    ImGui::Text("QC Status:");
    ImGui::SameLine();

    bool isPass = (currentRecord.qc_status == "Pass");
    bool isFail = (currentRecord.qc_status == "Fail");

    ImGui::PushStyleColor(ImGuiCol_Button, isPass ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    if (ImGui::Button("Pass (P)", ImVec2(80.0f * currentScale_, 30.0f * currentScale_)))
        markAsPass();
    ImGui::PopStyleColor();

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, isFail ? ImVec4(0.8f, 0.2f, 0.2f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    if (ImGui::Button("Fail (F)", ImVec2(80.0f * currentScale_, 30.0f * currentScale_)))
        markAsFail();
    ImGui::PopStyleColor();

    ImGui::Separator();

    ImGui::Text("Notes:");
    char notesBuffer[1024];
    strncpy(notesBuffer, currentRecord.notes.c_str(), sizeof(notesBuffer));
    notesBuffer[sizeof(notesBuffer) - 1] = '\0';

    if (ImGui::InputText("##Notes", notesBuffer, sizeof(notesBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        currentRecord.notes = notesBuffer;
        if (autoSave_) saveProgress();
    }

    ImGui::Separator();

    int completedCount = 0;
    for (const auto& record : records)
        if (!record.qc_status.empty()) completedCount++;

    ImGui::Text("%d / %d rated", completedCount, static_cast<int>(records.size()));

    float progress = static_cast<float>(completedCount) / static_cast<float>(records.size());
    ImGui::ProgressBar(progress, ImVec2(0, 20));

    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::Text("Shortcuts:");
    ImGui::Text("P - Mark as Pass");
    ImGui::Text("F - Mark as Fail");
    ImGui::Text("←/→ - Navigate");
    ImGui::Text("Ctrl+S - Save");
    ImGui::PopStyleColor();

    ImGui::Separator();

    renderCaseList();

    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("RightColumn", ImVec2(rightColumnWidth, 0), false);

    renderImage();

    ImGui::EndChild();

    ImGui::End();
}

void QCApp::renderImage()
{
    if (!currentImage_.texture || currentImage_.width == 0)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Failed to load image");
        return;
    }

    float availWidth = ImGui::GetContentRegionAvail().x;
    float availHeight = ImGui::GetContentRegionAvail().y;

    float imageAspect = static_cast<float>(currentImage_.width) / static_cast<float>(currentImage_.height);
    float displayWidth = availWidth;
    float displayHeight = displayWidth / imageAspect;

    if (displayHeight > availHeight)
    {
        displayHeight = availHeight;
        displayWidth = displayHeight * imageAspect;
    }

    float startX = (availWidth - displayWidth) * 0.5f;
    float startY = (availHeight - displayHeight) * 0.5f;

    ImGui::SetCursorPosX(startX);
    ImGui::SetCursorPosY(startY);

    ImGui::Image(currentImage_.texture->id, ImVec2(displayWidth, displayHeight));
}

void QCApp::navigateTo(size_t index)
{
    if (index < csvHandler_.getRecordCount())
    {
        currentIndex_ = index;
        scrollToCurrentRow_ = true;
        loadImage(csvHandler_.getRecords()[currentIndex_].picture_path);
    }
}

void QCApp::navigatePrevious()
{
    if (currentIndex_ > 0)
    {
        --currentIndex_;
        scrollToCurrentRow_ = true;
        loadImage(csvHandler_.getRecords()[currentIndex_].picture_path);
    }
}

void QCApp::navigateNext()
{
    if (currentIndex_ < csvHandler_.getRecordCount() - 1)
    {
        ++currentIndex_;
        scrollToCurrentRow_ = true;
        loadImage(csvHandler_.getRecords()[currentIndex_].picture_path);
    }
}

void QCApp::markAsPass()
{
    auto& records = csvHandler_.getRecords();
    records[currentIndex_].qc_status = "Pass";
    if (autoSave_) saveProgress();
    navigateNext();
}

void QCApp::markAsFail()
{
    auto& records = csvHandler_.getRecords();
    records[currentIndex_].qc_status = "Fail";
    if (autoSave_) saveProgress();
    navigateNext();
}

void QCApp::saveProgress()
{
    csvHandler_.saveOutputCSV(outputFile_);
}

void QCApp::handleKeyboard(int key, int scancode, int action, int mods)
{
    if (action != GLFW_PRESS && action != GLFW_REPEAT)
        return;

    switch (key)
    {
        case GLFW_KEY_P:         markAsPass(); break;
        case GLFW_KEY_F:         markAsFail(); break;
        case GLFW_KEY_LEFT:
        case GLFW_KEY_PAGE_UP:   navigatePrevious(); break;
        case GLFW_KEY_RIGHT:
        case GLFW_KEY_PAGE_DOWN: navigateNext(); break;
        case GLFW_KEY_S:
            if (mods & GLFW_MOD_CONTROL) saveProgress();
            break;
        case GLFW_KEY_ESCAPE:    running_ = false; break;
    }
}

void QCApp::glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    QCApp* app = reinterpret_cast<QCApp*>(glfwGetWindowUserPointer(window));
    if (app) app->handleKeyboard(key, scancode, action, mods);
}

void QCApp::glfwWindowSizeCallback(GLFWwindow* /*window*/, int /*width*/, int /*height*/)
{
    // Handled by needsSwapchainRebuild / rebuildSwapchain in run()
}

void QCApp::glfwCloseCallback(GLFWwindow* window)
{
    QCApp* app = reinterpret_cast<QCApp*>(glfwGetWindowUserPointer(window));
    if (app) app->running_ = false;
}

} // namespace QC
