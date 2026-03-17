#pragma once

// Direct wl_touch → ImGui IO injection for Wayland touch screen support.
// Only compiled when wayland-client headers are available (HAS_WAYLAND_TOUCH).
// Bypasses GLFW's own wl_touch synthesis, which is unreliable on 3.3.x and
// still broken on some 3.4 configurations.

#ifdef HAS_WAYLAND_TOUCH

struct GLFWwindow;

namespace WaylandTouch
{
    /// Install a wl_touch listener on the Wayland seat.
    /// Must be called after glfwInit(), window creation, and ImGui::CreateContext().
    /// Returns true if the seat advertises touch capability.
    bool install(GLFWwindow* window);

    /// Release Wayland objects acquired by install().
    /// Call before glfwDestroyWindow().
    void shutdown();
}

#endif // HAS_WAYLAND_TOUCH
