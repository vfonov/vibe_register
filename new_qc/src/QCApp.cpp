#include "QCApp.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

namespace QC
{

QCApp::QCApp() = default;

QCApp::~QCApp()
{
    shutdown();
}

bool QCApp::init(const std::string& inputFile, const std::string& outputFile, const std::optional<float>& scaleFactor)
{
    outputFile_ = outputFile;
    
    // Try to load existing output file first (resume work)
    if (!csvHandler_.loadOutputCSV(outputFile))
    {
        // If output doesn't exist, load input file
        if (!csvHandler_.loadInputCSV(inputFile))
        {
            return false;
        }
    }
    
    if (csvHandler_.getRecordCount() == 0)
    {
        std::cerr << "Error: No records to process" << std::endl;
        return false;
    }
    
    // Initialize GLFW
    if (!glfwInit())
    {
        std::cerr << "Error: Failed to initialize GLFW" << std::endl;
        std::cerr << "Note: new_qc requires a display (X11 or Wayland) to run." << std::endl;
        std::cerr << "If running in a headless environment, use a virtual framebuffer:" << std::endl;
        std::cerr << "  xvfb-run ./new_qc input.csv output.csv" << std::endl;
        return false;
    }
    
    // Configure GLFW
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    
    // Always enable GLFW_SCALE_TO_MONITOR for proper HiDPI framebuffer scaling
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    
    // Determine scale factor: command-line override takes precedence
    float initScale = 1.0f;
    bool scaleOverride = scaleFactor.has_value();
    
    if (scaleOverride)
    {
        initScale = scaleFactor.value();
        std::cout << "Using scale override: " << initScale << std::endl;
    }
    else
    {
        // Get monitor content scale
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
    
    // Calculate window size based on scale
    int initW = static_cast<int>(1280 * initScale);
    int initH = static_cast<int>(720 * initScale);
    
    // Create window
    window_ = glfwCreateWindow(initW, initH, "new_qc - Quality Control", nullptr, nullptr);
    if (!window_)
    {
        std::cerr << "Error: Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }
    
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1); // Enable vsync
    
    // Setup callbacks
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, glfwKeyCallback);
    glfwSetWindowSizeCallback(window_, glfwWindowSizeCallback);
    glfwSetWindowCloseCallback(window_, glfwCloseCallback);
    
    // Setup ImGui
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    ImGui::StyleColorsDark();
    
    // Apply DPI scale factor to ImGui for HiDPI support
    std::cout << "Applying ImGui scale: " << initScale << std::endl;
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(initScale);
    style.FontScaleDpi = initScale;
    currentScale_ = initScale;
    std::cout << "ImGui FontScaleDpi after setting: " << style.FontScaleDpi << std::endl;
    
    // Initialize ImGui backends
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    
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
        
        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        
        // Render UI
        renderUI();
        
        // Render
        ImGui::Render();
        int displayWidth, displayHeight;
        glfwGetFramebufferSize(window_, &displayWidth, &displayHeight);
        glViewport(0, 0, displayWidth, displayHeight);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        
        glfwSwapBuffers(window_);
    }
}

void QCApp::shutdown()
{
    if (currentImage_.texture != 0)
    {
        glDeleteTextures(1, &currentImage_.texture);
        currentImage_.texture = 0;
    }
    
    if (window_)
    {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    
    // Only shutdown ImGui if it was initialized
    if (window_)
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    
    glfwTerminate();
}

void QCApp::loadImage(const std::string& path)
{
    // Cleanup previous texture
    if (currentImage_.texture != 0)
    {
        glDeleteTextures(1, &currentImage_.texture);
        currentImage_.texture = 0;
    }
    currentImage_.pixels.clear();
    
    // Load image using stb_image
    int width, height, channels;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4); // Force RGBA
    
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
    currentImage_.pixels.assign(data, data + width * height * 4);
    stbi_image_free(data);
    
    // Create OpenGL texture
    glGenTextures(1, &currentImage_.texture);
    glBindTexture(GL_TEXTURE_2D, currentImage_.texture);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, currentImage_.pixels.data());
    
    std::cout << "Loaded image: " << path << " (" << width << "x" << height << ")" << std::endl;
}

void QCApp::renderCaseList()
{
    auto& records = csvHandler_.getRecords();
    int totalTableCols = 3; // # + ID + Visit
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
            
            // Color row by QC status
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
            {
                navigateTo(ri);
            }
            
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
    
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("##Main Window", nullptr, windowFlags);
    
    // Two-column layout: left column (controls + case list), right column (image)
    float leftColumnWidth = ImGui::CalcTextSize("Notes").x + 400.0f;
    leftColumnWidth *= currentScale_;
    float rightColumnWidth = ImGui::GetContentRegionAvail().x - leftColumnWidth;
    
    // Left column: control panel and case list
    ImGui::BeginChild("LeftColumn", ImVec2(leftColumnWidth, 0), true);
    
  auto& records = csvHandler_.getRecords();
    auto& currentRecord = records[currentIndex_];
    
    // Prev / Next navigation buttons
    float navBtnWidth = ImGui::CalcTextSize("Previous").x + 30.0f;
    float saveBtnWidth = ImGui::CalcTextSize("Save Results").x + 30.0f;
    bool atFirst = (currentIndex_ <= 0);
    bool atLast = (currentIndex_ >= records.size() - 1);
    
    if (atFirst) ImGui::BeginDisabled();
    if (ImGui::Button("Previous", ImVec2(navBtnWidth * currentScale_, 0)))
    {
        navigatePrevious();
    }
    if (atFirst) ImGui::EndDisabled();
    
    ImGui::SameLine();
    
    if (atLast) ImGui::BeginDisabled();
    if (ImGui::Button("Next", ImVec2(navBtnWidth * currentScale_, 0)))
    {
        navigateNext();
    }
    if (atLast) ImGui::EndDisabled();
    
    // Autosave checkbox + manual Save button
    ImGui::Checkbox("Autosave results", &autoSave_);
    if (ImGui::Button("Save Results", ImVec2(saveBtnWidth * currentScale_, 0)))
    {
        saveProgress();
    }
    
    ImGui::Separator();
    ImGui::Text("ID: %s", currentRecord.id.c_str());
    ImGui::Separator();
    ImGui::Text("Visit: %s", currentRecord.visit.c_str());
    ImGui::Separator();
    ImGui::Text("Image: %s", currentRecord.picture_path.c_str());
    ImGui::Separator();
    
    // QC Status verdict panel
    ImGui::Separator();
    ImGui::Text("QC Status:");
    ImGui::SameLine();
    
    bool isPass = (currentRecord.qc_status == "Pass");
    bool isFail = (currentRecord.qc_status == "Fail");
    
    ImGui::PushStyleColor(ImGuiCol_Button, isPass ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    if (ImGui::Button("Pass (P)", ImVec2(80.0f * currentScale_, 30.0f * currentScale_)))
    {
        markAsPass();
    }
    ImGui::PopStyleColor();
    
    ImGui::SameLine();
    
    ImGui::PushStyleColor(ImGuiCol_Button, isFail ? ImVec4(0.8f, 0.2f, 0.2f, 1.0f) : ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    if (ImGui::Button("Fail (F)", ImVec2(80.0f * currentScale_, 30.0f * currentScale_)))
    {
        markAsFail();
    }
    ImGui::PopStyleColor();
    
    ImGui::Separator();
    
    // Notes field
    ImGui::Text("Notes:");
    char notesBuffer[1024];
    strncpy(notesBuffer, currentRecord.notes.c_str(), sizeof(notesBuffer));
    notesBuffer[sizeof(notesBuffer) - 1] = '\0';
    
    if (ImGui::InputText("##Notes", notesBuffer, sizeof(notesBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        currentRecord.notes = notesBuffer;
        if (autoSave_)
        {
            saveProgress();
        }
    }
    
    ImGui::Separator();
    
    // Progress indicator
    int completedCount = 0;
    for (const auto& record : records)
    {
        if (!record.qc_status.empty())
            completedCount++;
    }
    
    ImGui::Text("%d / %d rated", completedCount, static_cast<int>(records.size()));
    
    float progress = static_cast<float>(completedCount) / static_cast<float>(records.size());
    ImGui::ProgressBar(progress, ImVec2(0, 20));
    
    // Keyboard shortcuts help
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::Text("Shortcuts:");
    ImGui::Text("P - Mark as Pass");
    ImGui::Text("F - Mark as Fail");
    ImGui::Text("←/→ - Navigate");
    ImGui::Text("Ctrl+S - Save");
    ImGui::PopStyleColor();
    
    ImGui::Separator();
    
    // Case list
    renderCaseList();
    
    ImGui::EndChild();
    
    // Right column: image display
    ImGui::SameLine();
    ImGui::BeginChild("RightColumn", ImVec2(rightColumnWidth, 0), false);
    
    // Render image
    renderImage();
    
    ImGui::EndChild();
    
    ImGui::End();
}

void QCApp::renderImage()
{
    if (currentImage_.texture == 0 || currentImage_.width == 0)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Failed to load image");
        return;
    }
    
    ImGuiIO& io = ImGui::GetIO();
    float availWidth = ImGui::GetContentRegionAvail().x;
    float availHeight = ImGui::GetContentRegionAvail().y;
    
    // Calculate scaled dimensions
    float imageAspect = static_cast<float>(currentImage_.width) / static_cast<float>(currentImage_.height);
    float displayWidth = availWidth;
    float displayHeight = displayWidth / imageAspect;
    
    if (displayHeight > availHeight)
    {
        displayHeight = availHeight;
        displayWidth = displayHeight * imageAspect;
    }
    
    // Center the image
    float startX = (availWidth - displayWidth) * 0.5f;
    float startY = (availHeight - displayHeight) * 0.5f;
    
    ImGui::SetCursorPosX(startX);
    ImGui::SetCursorPosY(startY);
    
    ImTextureID texId = (ImTextureID)(uintptr_t)currentImage_.texture;
    ImGui::Image(texId, ImVec2(displayWidth, displayHeight));
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
    
    if (autoSave_)
    {
        saveProgress();
    }
    
    // Move to next image
    navigateNext();
}

void QCApp::markAsFail()
{
    auto& records = csvHandler_.getRecords();
    records[currentIndex_].qc_status = "Fail";
    
    if (autoSave_)
    {
        saveProgress();
    }
    
    // Move to next image
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
        case GLFW_KEY_P:
            markAsPass();
            break;
        
        case GLFW_KEY_F:
            markAsFail();
            break;
        
        case GLFW_KEY_LEFT:
        case GLFW_KEY_PAGE_UP:
            navigatePrevious();
            break;
        
        case GLFW_KEY_RIGHT:
        case GLFW_KEY_PAGE_DOWN:
            navigateNext();
            break;
        
        case GLFW_KEY_S:
            if (mods & GLFW_MOD_CONTROL)
            {
                saveProgress();
            }
            break;
        
        case GLFW_KEY_ESCAPE:
            running_ = false;
            break;
    }
}

void QCApp::glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    QCApp* app = reinterpret_cast<QCApp*>(glfwGetWindowUserPointer(window));
    if (app)
    {
        app->handleKeyboard(key, scancode, action, mods);
    }
}

void QCApp::glfwWindowSizeCallback(GLFWwindow* window, int width, int height)
{
    // Window resize handled automatically by ImGui
}

void QCApp::glfwCloseCallback(GLFWwindow* window)
{
    QCApp* app = reinterpret_cast<QCApp*>(glfwGetWindowUserPointer(window));
    if (app)
    {
        app->running_ = false;
    }
}

} // namespace QC
