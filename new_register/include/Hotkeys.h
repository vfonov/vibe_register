#pragma once

#include <imgui.h>

/// True on the frame a single-key shortcut fires: once per physical press
/// (key repeat is ignored) and only without Ctrl/Alt/Super held, so chords
/// such as Ctrl+C never trigger it. Shift is allowed ('?' is Shift+/).
inline bool hotkeyPressed(ImGuiKey key)
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl || io.KeyAlt || io.KeySuper)
        return false;
    return ImGui::IsKeyPressed(key, false);
}
