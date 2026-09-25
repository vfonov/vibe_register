/// test_hotkeys.cpp — single-key shortcuts (R, Q, C, P, H, ?).
///
/// Drives a bare ImGui context (no renderer) frame by frame. Holding a key
/// must fire its shortcut once, not on every key repeat (holding C used to
/// flicker clean mode, holding P wrote a screenshot per repeat), and
/// Ctrl/Alt/Super chords such as Ctrl+C must not fire it at all.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <imgui.h>

#include "Hotkeys.h"

namespace
{

/// Owns an ImGui context that can run frames without a renderer backend.
class ImGuiFrames
{
public:
    ImGuiFrames()
    {
        ctx_ = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(640.0f, 480.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    }
    ~ImGuiFrames() { ImGui::DestroyContext(ctx_); }

    /// Run `frames` frames and count those where `key` fires.
    int countPresses(ImGuiKey key, int frames)
    {
        int presses = 0;
        for (int f = 0; f < frames; ++f)
        {
            ImGui::NewFrame();
            if (hotkeyPressed(key))
                ++presses;
            ImGui::EndFrame();
        }
        return presses;
    }

private:
    ImGuiContext* ctx_;
};

}  // namespace

TEST_CASE("Holding a hotkey fires it once", "[hotkeys]")
{
    ImGuiFrames ui;
    ImGui::GetIO().AddKeyEvent(ImGuiKey_C, true);
    // One second at 60 fps: well past the key-repeat delay.
    CHECK(ui.countPresses(ImGuiKey_C, 60) == 1);

    ImGui::GetIO().AddKeyEvent(ImGuiKey_C, false);
    CHECK(ui.countPresses(ImGuiKey_C, 2) == 0);

    ImGui::GetIO().AddKeyEvent(ImGuiKey_C, true);  // a second press fires again
    CHECK(ui.countPresses(ImGuiKey_C, 2) == 1);
}

TEST_CASE("Hotkeys ignore Ctrl, Alt and Super chords", "[hotkeys]")
{
    const ImGuiKey mod = GENERATE(ImGuiMod_Ctrl, ImGuiMod_Alt, ImGuiMod_Super);
    ImGuiFrames ui;
    ImGui::GetIO().AddKeyEvent(mod, true);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_C, true);
    CHECK(ui.countPresses(ImGuiKey_C, 2) == 0);
}

TEST_CASE("Shift does not block a hotkey ('?' is Shift+/)", "[hotkeys]")
{
    ImGuiFrames ui;
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, true);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Slash, true);
    CHECK(ui.countPresses(ImGuiKey_Slash, 2) == 1);
}
