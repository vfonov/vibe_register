// generate_headless_baselines.cpp — Generate baseline PNGs for headless_ui_tests
//
// Build: Already configured in CMakeLists.txt as generate_headless_baselines
// Run:  LIBGL_ALWAYS_SOFTWARE=1 ./generate_headless_baselines
//
// This tool generates reference PNG screenshots that are used by
// headless_ui_tests for pixel-perfect visual regression testing.

#include <GL/osmesa.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include <cstdio>
#include <functional>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui_impl_osmesa.h>

// Forward-declare imgl3wInit2 from imgui_impl_opengl3_loader.h
typedef void        (*GL3WglProc_)(void);
typedef GL3WglProc_ (*GL3WGetProcAddressProc_)(const char *proc);

extern "C" int imgl3wInit2(GL3WGetProcAddressProc_ proc);

static GL3WglProc_ osmesa_get_proc(const char* name)
{
    return reinterpret_cast<GL3WglProc_>(OSMesaGetProcAddress(name));
}

static constexpr int WIDTH  = 1280;
static constexpr int HEIGHT = 720;

// Render one ImGui frame using the supplied content callback
static void run_frame(std::function<void()> content)
{
    ImGuiIO& io    = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)WIDTH, (float)HEIGHT);
    io.DeltaTime   = 1.0f / 60.0f;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    content();

    ImGui::Render();

    glViewport(0, 0, WIDTH, HEIGHT);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glFinish();
}

// Generate baseline for test_window_appears
static void generate_window_appears_baseline()
{
    run_frame([]()
    {
        ImGui::Begin("Test Window");
        ImGui::Text("hello from headless test");
        ImGui::End();
    });

    osmesa_save_png("/app/new_register/tests/baseline_window_appears.png");
    printf("Generated baseline_window_appears.png\n");
}

// Generate baseline for test_no_scroll_bars
static void generate_no_scroll_bars_baseline()
{
    // Render UI panel layout that previously showed scroll bars
    bool checkbox = false;
    run_frame([&]()
    {
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(300.0f, 400.0f), ImGuiCond_Always);
        ImGui::Begin("##controls", nullptr, 
            ImGuiWindowFlags_NoScrollbar | 
            ImGuiWindowFlags_NoScrollWithMouse);
        
        for (int i = 0; i < 20; i++)
        {
            ImGui::Text("Control item %d", i);
            ImGui::Checkbox("Checkbox", &checkbox);
        }
        ImGui::End();
    });

    osmesa_save_png("/app/new_register/tests/baseline_no_scroll_bars.png");
    printf("Generated baseline_no_scroll_bars.png\n");
}

// Generate baseline for test_button_click (frame 4 - final state)
static void generate_button_click_baseline()
{
    ImGuiIO& io = ImGui::GetIO();

    // Frame 1
    run_frame([&]()
    {
        ImGui::SetNextWindowPos(ImVec2(50.0f, 50.0f), ImGuiCond_Always);
        ImGui::Begin("Button Test", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::End();
    });

    ImVec2 btn_pos(0.0f, 0.0f);
    run_frame([&]()
    {
        ImGui::SetNextWindowPos(ImVec2(50.0f, 50.0f), ImGuiCond_Always);
        ImGui::Begin("Button Test", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        btn_pos = ImGui::GetCursorScreenPos();
        ImGui::End();
    });

    // Frame 2: hover
    io.AddMousePosEvent(btn_pos.x + 5.0f, btn_pos.y + 5.0f);
    run_frame([&]()
    {
        ImGui::SetNextWindowPos(ImVec2(50.0f, 50.0f), ImGuiCond_Always);
        ImGui::Begin("Button Test", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::End();
    });

    // Frame 3: press
    io.AddMouseButtonEvent(0, true);
    run_frame([&]()
    {
        ImGui::SetNextWindowPos(ImVec2(50.0f, 50.0f), ImGuiCond_Always);
        ImGui::Begin("Button Test", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::End();
    });

    // Frame 4: release (clicked state) - THIS is our baseline
    io.AddMouseButtonEvent(0, false);
    run_frame([&]()
    {
        ImGui::SetNextWindowPos(ImVec2(50.0f, 50.0f), ImGuiCond_Always);
        ImGui::Begin("Button Test", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Button("Click Me");
        ImGui::End();
    });

    osmesa_save_png("/app/new_register/tests/baseline_button_click.png");
    printf("Generated baseline_button_click.png\n");
}

int main()
{
    if (!osmesa_init(WIDTH, HEIGHT))
    {
        fprintf(stderr, "FATAL: OSMesa context creation failed\n");
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io    = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)WIDTH, (float)HEIGHT);
    io.DeltaTime   = 1.0f / 60.0f;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    if (imgl3wInit2(osmesa_get_proc) != 0)
    {
        fprintf(stderr, "FATAL: imgl3wInit2 (OSMesa) failed\n");
        ImGui::DestroyContext();
        osmesa_shutdown();
        return 1;
    }

    if (!ImGui_ImplOpenGL3_Init("#version 130"))
    {
        fprintf(stderr, "FATAL: ImGui_ImplOpenGL3_Init failed\n");
        ImGui::DestroyContext();
        osmesa_shutdown();
        return 1;
    }

  generate_window_appears_baseline();
    generate_no_scroll_bars_baseline();
    generate_button_click_baseline();

    printf("Baseline generation complete.\n");
    return 0;
}
