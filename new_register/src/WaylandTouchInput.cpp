// WaylandTouchInput.cpp — inject wl_touch events directly into ImGui IO.
//
// GLFW's own wl_touch → synthetic-mouse path is unreliable on 3.3.x and
// sometimes broken on 3.4 as well.  This implementation bypasses GLFW
// entirely for touch events: it registers its own wl_touch listener on the
// same wl_display connection that GLFW uses, then calls ImGui's AddMouse*
// events directly.  GLFW's own dispatch loop (glfwPollEvents) dispatches all
// pending events on the shared wl_display, so our callbacks are called
// automatically during the normal render loop — no extra threading needed.

#ifdef HAS_WAYLAND_TOUCH

#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <wayland-client.h>
#include <imgui.h>

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
    if (id != 0) return;            // track first touch point only → left mouse button
    if (surface != s.surface) return;  // event is for a different surface

    s.active = true;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(static_cast<float>(wl_fixed_to_double(x)),
                        static_cast<float>(wl_fixed_to_double(y)));
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
}

static void touch_up(void*, wl_touch*, uint32_t /*serial*/, uint32_t /*time*/,
                     int32_t id)
{
    if (id != 0 || !s.active) return;
    s.active = false;
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
}

static void touch_motion(void*, wl_touch*, uint32_t /*time*/,
                         int32_t id, wl_fixed_t x, wl_fixed_t y)
{
    if (id != 0) return;
    ImGui::GetIO().AddMousePosEvent(static_cast<float>(wl_fixed_to_double(x)),
                                    static_cast<float>(wl_fixed_to_double(y)));
}

static void touch_frame(void*, wl_touch*) {}

static void touch_cancel(void*, wl_touch*)
{
    if (!s.active) return;
    s.active = false;
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
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
    wl_display* display = glfwGetWaylandDisplay();
    if (!display)
        return false;

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
}

void shutdown()
{
    if (s.touch)    { wl_touch_destroy(s.touch);       s.touch    = nullptr; }
    if (s.seat)     { wl_seat_destroy(s.seat);          s.seat     = nullptr; }
    if (s.registry) { wl_registry_destroy(s.registry);  s.registry = nullptr; }
    s.active  = false;
    s.surface = nullptr;
}

} // namespace WaylandTouch

#endif // HAS_WAYLAND_TOUCH
