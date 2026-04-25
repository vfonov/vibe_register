/// test_column_layout.cpp — headless test for dock column equal-width layout
///
/// Verifies that after a DockBuilder rebuild (e.g. overlay toggle, clean
/// mode toggle) all volume-column windows have approximately equal widths
/// and fill the expected portion of the viewport.
///
/// Note: ini persistence is disabled (io.IniFilename = nullptr) per the
/// headless test convention.  The ini-based stale-size bug is therefore
/// not reproduced here, but the equal-width invariant and the clean-mode
/// full-width expansion are useful regression gates.

#include <GL/osmesa.h>
#include <GL/gl.h>

#include <cstdio>
#include <cmath>
#include <functional>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui_impl_osmesa.h"

typedef void        (*GL3WglProc_)(void);
typedef GL3WglProc_ (*GL3WGetProcAddressProc_)(const char *proc);
extern "C" int imgl3wInit2(GL3WGetProcAddressProc_ proc);

static constexpr int   WIDTH  = 1280;
static constexpr int   HEIGHT = 720;
static constexpr float TOOLS_FRACTION = 0.23f;  // normal-mode tools width

// Stable dockspace ID — does not use DockSpaceOverViewport to avoid
// requiring a platform backend (GLFW) that headless tests do not set up.
static const ImGuiID DOCKSPACE_ID = ImHashStr("TestColumnLayout");

static GL3WglProc_ osmesa_get_proc(const char* name)
{
    return reinterpret_cast<GL3WglProc_>(OSMesaGetProcAddress(name));
}

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
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glFinish();
}

// Emit the fullscreen dockspace host window each frame.
static void render_dockspace_host()
{
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2((float)WIDTH, (float)HEIGHT));
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##dockhost", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove   |
        ImGuiWindowFlags_NoDocking    | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    ImGui::DockSpace(DOCKSPACE_ID, ImVec2(0.0f, 0.0f),
        ImGuiDockNodeFlags_AutoHideTabBar);
    ImGui::End();
}

// Rebuild the column dock layout, mirroring Interface.cpp (including the fix).
// cleanMode=true: no tools/tags split — columns fill the full viewport.
static void build_column_layout(int totalColumns, bool cleanMode = false)
{
    const ImVec2 vpSize{(float)WIDTH, (float)HEIGHT};
    const float  tf = cleanMode ? 0.0f : TOOLS_FRACTION;

    ImGui::DockBuilderRemoveNode(DOCKSPACE_ID);
    ImGui::DockBuilderAddNode(DOCKSPACE_ID, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(DOCKSPACE_ID, vpSize);

    ImGuiID contentId;
    if (cleanMode) {
        // Clean mode: columns fill the full viewport (mirrors the fix in Interface.cpp).
        contentId = DOCKSPACE_ID;
    } else {
        ImGuiID toolsId;
        ImGui::DockBuilderSplitNode(DOCKSPACE_ID, ImGuiDir_Left, tf, &toolsId, &contentId);
        ImGui::DockBuilderDockWindow("Tools", toolsId);
    }

    std::vector<ImGuiID> colIds(totalColumns);
    if (totalColumns == 1) {
        colIds[0] = contentId;
    } else {
        ImGuiID rem = contentId;
        for (int ci = 0; ci < totalColumns - 1; ++ci) {
            float frac = 1.0f / float(totalColumns - ci);
            ImGuiID leftId, rightId;
            ImGui::DockBuilderSplitNode(rem, ImGuiDir_Left, frac, &leftId, &rightId);
            colIds[ci] = leftId;
            rem = rightId;
        }
        colIds[totalColumns - 1] = rem;
    }

    // Anchor each column's pixel width (mirrors the Interface.cpp fix).
    float colW = vpSize.x * (1.0f - tf) / float(totalColumns);
    for (int ci = 0; ci < totalColumns; ++ci)
        ImGui::DockBuilderSetNodeSize(colIds[ci], ImVec2(colW, vpSize.y));

    const char* colNames[] = {"Col0", "Col1", "Col2"};
    for (int ci = 0; ci < totalColumns; ++ci)
        ImGui::DockBuilderDockWindow(colNames[ci], colIds[ci]);

    ImGui::DockBuilderFinish(DOCKSPACE_ID);
}

// Check that numCols column windows have approximately equal widths and
// are each close to the expected per-column width derived from toolsFraction.
static bool check_columns_equal(int numCols, float toolsFraction, float tolerancePx)
{
    const char* colNames[] = {"Col0", "Col1", "Col2"};
    float expectedW = (float)WIDTH * (1.0f - toolsFraction) / float(numCols);
    float w0 = -1.0f;

    for (int ci = 0; ci < numCols; ++ci) {
        ImGuiWindow* win = ImGui::FindWindowByName(colNames[ci]);
        if (!win) {
            fprintf(stderr, "FAIL: window '%s' not found\n", colNames[ci]);
            return false;
        }
        float w = win->Size.x;
        fprintf(stderr, "  %s width=%.1f (expected ~%.1f)\n", colNames[ci], w, expectedW);

        if (w < expectedW * 0.80f || w > expectedW * 1.20f) {
            fprintf(stderr, "FAIL: %s width %.1f is >20%% off expected %.1f\n",
                    colNames[ci], w, expectedW);
            return false;
        }
        if (ci == 0) {
            w0 = w;
        } else if (fabsf(w - w0) > tolerancePx) {
            fprintf(stderr, "FAIL: %s (%.1f) differs from Col0 (%.1f) by %.1f px"
                    " (tolerance %.1f)\n",
                    colNames[ci], w, w0, fabsf(w - w0), tolerancePx);
            return false;
        }
    }
    return true;
}

// -----------------------------------------------------------------------
// Test cases
// -----------------------------------------------------------------------

// Three-column normal-mode layout → all widths equal.
static bool test_three_columns_equal()
{
    fprintf(stderr, "\n=== TEST: three_columns_equal ===\n");

    bool built = false;
    for (int f = 0; f < 5; ++f) {
        run_frame([&]() {
            render_dockspace_host();
            if (!built) { built = true; build_column_layout(3); }
            const char* cols[] = {"Col0", "Col1", "Col2"};
            for (const char* c : cols) { ImGui::Begin(c); ImGui::End(); }
            ImGui::Begin("Tools"); ImGui::End();
        });
    }

    return check_columns_equal(3, TOOLS_FRACTION, 5.0f);
}

// 3→2 column rebuild (overlay removed) → remaining widths equal.
static bool test_two_columns_after_toggle()
{
    fprintf(stderr, "\n=== TEST: two_columns_after_toggle ===\n");

    bool built3 = false;
    bool built2 = false;

    for (int f = 0; f < 4; ++f) {
        run_frame([&]() {
            render_dockspace_host();
            if (!built3) { built3 = true; build_column_layout(3); }
            const char* cols[] = {"Col0", "Col1", "Col2"};
            for (const char* c : cols) { ImGui::Begin(c); ImGui::End(); }
            ImGui::Begin("Tools"); ImGui::End();
        });
    }

    for (int f = 0; f < 5; ++f) {
        run_frame([&]() {
            render_dockspace_host();
            if (!built2) { built2 = true; build_column_layout(2); }
            const char* cols[] = {"Col0", "Col1"};
            for (const char* c : cols) { ImGui::Begin(c); ImGui::End(); }
            ImGui::Begin("Tools"); ImGui::End();
        });
    }

    return check_columns_equal(2, TOOLS_FRACTION, 5.0f);
}

// Clean mode: columns fill full viewport (no tools fraction reserved).
static bool test_clean_mode_full_width()
{
    fprintf(stderr, "\n=== TEST: clean_mode_full_width ===\n");

    bool built = false;
    for (int f = 0; f < 5; ++f) {
        run_frame([&]() {
            render_dockspace_host();
            if (!built) { built = true; build_column_layout(2, true); }
            const char* cols[] = {"Col0", "Col1"};
            for (const char* c : cols) { ImGui::Begin(c); ImGui::End(); }
            // Tools window absent in clean mode.
        });
    }

    // toolsFraction=0: each column should be WIDTH/2.
    return check_columns_equal(2, 0.0f, 5.0f);
}

// Normal → clean mode transition: columns widen to fill full viewport.
static bool test_clean_mode_after_toggle()
{
    fprintf(stderr, "\n=== TEST: clean_mode_after_toggle ===\n");

    bool builtNormal = false;
    bool builtClean  = false;

    // Settle normal 2-column layout.
    for (int f = 0; f < 4; ++f) {
        run_frame([&]() {
            render_dockspace_host();
            if (!builtNormal) { builtNormal = true; build_column_layout(2, false); }
            const char* cols[] = {"Col0", "Col1"};
            for (const char* c : cols) { ImGui::Begin(c); ImGui::End(); }
            ImGui::Begin("Tools"); ImGui::End();
        });
    }

    fprintf(stderr, "  [normal mode]\n");
    if (!check_columns_equal(2, TOOLS_FRACTION, 5.0f))
        return false;

    // Switch to clean mode — rebuild with toolsFraction=0.
    for (int f = 0; f < 5; ++f) {
        run_frame([&]() {
            render_dockspace_host();
            if (!builtClean) { builtClean = true; build_column_layout(2, true); }
            const char* cols[] = {"Col0", "Col1"};
            for (const char* c : cols) { ImGui::Begin(c); ImGui::End(); }
        });
    }

    fprintf(stderr, "  [clean mode]\n");
    return check_columns_equal(2, 0.0f, 5.0f);
}

// -----------------------------------------------------------------------

int main()
{
    if (!osmesa_init(WIDTH, HEIGHT)) {
        fprintf(stderr, "FATAL: OSMesa init failed\n");
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io    = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.DisplaySize = ImVec2((float)WIDTH, (float)HEIGHT);
    io.DeltaTime   = 1.0f / 60.0f;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    if (imgl3wInit2(osmesa_get_proc) != 0) {
        fprintf(stderr, "FATAL: imgl3wInit2 failed\n");
        ImGui::DestroyContext();
        osmesa_shutdown();
        return 1;
    }

    if (!ImGui_ImplOpenGL3_Init("#version 130")) {
        fprintf(stderr, "FATAL: ImGui_ImplOpenGL3_Init failed\n");
        ImGui::DestroyContext();
        osmesa_shutdown();
        return 1;
    }

    struct Test { const char* name; bool (*fn)(); };
    const Test tests[] = {
        {"three_columns_equal",      test_three_columns_equal},
        {"two_columns_after_toggle", test_two_columns_after_toggle},
        {"clean_mode_full_width",    test_clean_mode_full_width},
        {"clean_mode_after_toggle",  test_clean_mode_after_toggle},
    };

    int failed = 0;
    for (const auto& t : tests) {
        ImGui::GetIO().ClearEventsQueue();
        bool ok = t.fn();
        printf("[%s] %s\n", ok ? "PASS" : "FAIL", t.name);
        if (!ok) ++failed;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    osmesa_shutdown();
    return (failed == 0) ? 0 : 1;
}
