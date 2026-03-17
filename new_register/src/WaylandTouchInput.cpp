// WaylandTouchInput.cpp — inject wl_touch events directly into ImGui IO.
//
// GLFW's own wl_touch → synthetic-mouse path is unreliable on 3.3.x and
// sometimes broken on 3.4 as well.  This implementation bypasses GLFW
// entirely for touch events: it registers its own wl_touch listener on the
// same wl_display connection that GLFW uses, then fires the ImGui GLFW
// backend callbacks (ImGui_ImplGlfw_CursorPosCallback / MouseButtonCallback)
// directly.  This keeps ImGui's internal MouseLastValidPos in sync so that
// ImGui_ImplGlfw_NewFrame() does NOT overwrite the touch position with the
// stale value returned by glfwGetCursorPos() — which is why buttons and
// checkboxes were ignored even though cursor movement worked.
//
// GLFW's own dispatch loop (glfwPollEvents) dispatches all pending events on
// the shared wl_display, so our callbacks are invoked automatically during
// the normal render loop — no extra threading needed.

#ifdef HAS_WAYLAND_TOUCH

#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <backends/imgui_impl_glfw.h>
#include <wayland-client.h>

#include <algorithm>
#include <cstring>
#include <cstdio>

namespace WaylandTouch
{

struct State
{
    wl_registry* registry = nullptr;
    wl_seat*     seat     = nullptr;
    wl_touch*    touch    = nullptr;
    wl_surface*  surface  = nullptr;  // our window's Wayland surface
    GLFWwindow*  window   = nullptr;  // needed to call ImGui GLFW callbacks
    bool         active   = false;    // finger currently down
};

static State s;

// ---------------------------------------------------------------------------
// wl_touch listener
// ---------------------------------------------------------------------------

static void touch_down(void*, wl_touch*, uint32_t /*serial*/, uint32_t /*time*/,
                       wl_surface* surface, int32_t id,
                       wl_fixed_t x, wl_fixed_t y)
{
    if (id != 0) return;               // track first touch point only → left mouse button
    if (surface != s.surface) return;  // event is for a different surface

    s.active = true;
    // ImGui_ImplGlfw_UpdateMouseData() calls glfwGetCursorPos() as a fallback
    // whenever bd->MouseWindow == nullptr (i.e. no wl_pointer has entered the
    // window).  That stale position is posted AFTER our touch position and
    // overwrites it, so ButtonBehavior never sees the cursor over the widget.
    // Calling CursorEnterCallback(GLFW_TRUE) sets bd->MouseWindow = window,
    // which suppresses the fallback for this frame.
    ImGui_ImplGlfw_CursorEnterCallback(s.window, GLFW_TRUE);
    ImGui_ImplGlfw_CursorPosCallback(s.window,
                                     wl_fixed_to_double(x),
                                     wl_fixed_to_double(y));
    ImGui_ImplGlfw_MouseButtonCallback(s.window,
                                       GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
}

static void touch_up(void*, wl_touch*, uint32_t /*serial*/, uint32_t /*time*/,
                     int32_t id)
{
    if (id != 0 || !s.active) return;
    s.active = false;
    ImGui_ImplGlfw_MouseButtonCallback(s.window,
                                       GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
}

static void touch_motion(void*, wl_touch*, uint32_t /*time*/,
                         int32_t id, wl_fixed_t x, wl_fixed_t y)
{
    if (id != 0) return;
    ImGui_ImplGlfw_CursorPosCallback(s.window,
                                     wl_fixed_to_double(x),
                                     wl_fixed_to_double(y));
}

static void touch_frame(void*, wl_touch*) {}

static void touch_cancel(void*, wl_touch*)
{
    if (!s.active) return;
    s.active = false;
    ImGui_ImplGlfw_MouseButtonCallback(s.window,
                                       GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
}

// shape and orientation were added in wl_touch version 6
static void touch_shape(void*, wl_touch*, int32_t, wl_fixed_t, wl_fixed_t) {}
static void touch_orientation(void*, wl_touch*, int32_t, wl_fixed_t) {}

static const wl_touch_listener touch_listener = {
    touch_down,
    touch_up,
    touch_motion,
    touch_frame,
    touch_cancel,
#ifdef WL_TOUCH_SHAPE_SINCE_VERSION
    touch_shape,
    touch_orientation,
#endif
};

// ---------------------------------------------------------------------------
// wl_seat listener
// ---------------------------------------------------------------------------

static void seat_capabilities(void*, wl_seat* seat, uint32_t caps)
{
    if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !s.touch)
    {
        s.touch = wl_seat_get_touch(seat);
        wl_touch_add_listener(s.touch, &touch_listener, nullptr);
    }
    else if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && s.touch)
    {
        wl_touch_destroy(s.touch);
        s.touch = nullptr;
    }
}

static void seat_name(void*, wl_seat*, const char*) {}

static const wl_seat_listener seat_listener = { seat_capabilities, seat_name };

// ---------------------------------------------------------------------------
// wl_registry listener
// ---------------------------------------------------------------------------

static void registry_global(void*, wl_registry* registry,
                             uint32_t name, const char* interface,
                             uint32_t version)
{
    if (strcmp(interface, wl_seat_interface.name) == 0 && !s.seat)
    {
        s.seat = static_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface,
                             std::min(version, 5u)));
        wl_seat_add_listener(s.seat, &seat_listener, nullptr);
    }
}

static void registry_global_remove(void*, wl_registry*, uint32_t) {}

static const wl_registry_listener registry_listener = {
    registry_global,
    registry_global_remove,
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool install(GLFWwindow* window)
{
#if GLFW_VERSION_MAJOR < 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR < 4)
    // glfwGetWaylandDisplay / glfwGetWaylandWindow require GLFW 3.4+;
    // system 3.3.x may lack Wayland support in its library.
    (void)window;
    return false;
#else
    wl_display* display = glfwGetWaylandDisplay();
    if (!display)
        return false;

    s.window   = window;
    s.surface  = glfwGetWaylandWindow(window);
    s.registry = wl_display_get_registry(display);
    if (!s.registry)
        return false;

    wl_registry_add_listener(s.registry, &registry_listener, nullptr);
    wl_display_roundtrip(display);  // populate registry globals → binds seat
    wl_display_roundtrip(display);  // seat capabilities arrive in second round

    if (!s.touch)
    {
        fprintf(stderr, "WaylandTouch: seat has no touch capability\n");
        return false;
    }

    return true;
#endif // GLFW_VERSION_MINOR >= 4
}

void shutdown()
{
    if (s.touch)    { wl_touch_destroy(s.touch);       s.touch    = nullptr; }
    if (s.seat)     { wl_seat_destroy(s.seat);          s.seat     = nullptr; }
    if (s.registry) { wl_registry_destroy(s.registry);  s.registry = nullptr; }
    s.active  = false;
    s.window  = nullptr;
    s.surface = nullptr;
}

} // namespace WaylandTouch

#endif // HAS_WAYLAND_TOUCH
