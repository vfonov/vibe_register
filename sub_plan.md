# sub_plan.md — Multi-Backend Support: Detailed Implementation Plan

This document specifies exact code changes, insertion points, and new file contents
for adding multiple graphics backend support (Vulkan + OpenGL 2) to `new_register`,
with the architecture designed to accommodate Metal in the future.

## Design Decisions

- **Texture abstraction**: Opaque handle — `Texture` struct with `ImTextureID` + dimensions.
  All GPU-specific details stay inside the backend implementation.
- **Backend selection**: Hybrid — CMake options control which backends are compiled;
  runtime auto-detection picks the best available, with CLI override.
- **OpenGL scope**: OpenGL 2 only (fixed-function pipeline, widest legacy/SSH support).
- **Metal**: Deferred — architecture supports it, not implemented yet.

## Current Vulkan Leakage (to be fixed)

These are the exact locations where Vulkan-specific types have leaked outside the
backend files (`VulkanBackend.cpp`, `VulkanHelpers.cpp`):

| File | Line(s) | Leak |
|---|---|---|
| `AppState.h` | 11, 19, 36 | `#include "VulkanHelpers.h"`, `unique_ptr<VulkanTexture>` in `VolumeViewState` and `OverlayState` |
| `ViewManager.cpp` | 8, 108-116, 259-267 | `#include "VulkanHelpers.h"`, direct calls to `VulkanHelpers::CreateTexture/UpdateTexture/DestroyTexture` |
| `Interface.cpp` | 975, 1022, 1341, 1388 | `VulkanTexture*` dereference, `reinterpret_cast<ImTextureID>(tex->descriptor_set)` |
| `main.cpp` | 16, 26, 376 | `#define GLFW_INCLUDE_VULKAN`, `#include "VulkanHelpers.h"`, `VulkanHelpers::Shutdown()` |

---

## Phase 1: Abstract the Texture System ✅ COMPLETE (commit `26df1fb`)

The critical prerequisite. Replace all `VulkanTexture` references outside backend files
with a backend-agnostic `Texture` type. After this phase, the only files that know about
Vulkan are `VulkanBackend.cpp`, `VulkanBackend.h`, `VulkanHelpers.cpp`, `VulkanHelpers.h`.

### 1.1 Add `Texture` struct to `GraphicsBackend.h`

**File: `include/GraphicsBackend.h`** — Add before the `GraphicsBackend` class (after
the includes, around line 7):

```cpp
#include <imgui.h>  // for ImTextureID

/// Backend-agnostic texture handle.
/// The `id` field is an opaque handle whose meaning depends on the backend:
///   - Vulkan: VkDescriptorSet cast to ImTextureID
///   - OpenGL: GLuint texture name cast to ImTextureID
///   - Metal:  MTLTexture* cast to ImTextureID
/// Application code should never interpret `id` — only pass it to ImGui::Image().
struct Texture
{
    ImTextureID id = nullptr;
    int width  = 0;
    int height = 0;
};
```

### 1.2 Add texture lifecycle methods to `GraphicsBackend` interface

**File: `include/GraphicsBackend.h`** — Add after the `captureScreenshot` method
(before the Factory section, around line 80):

```cpp
    // --- Texture management ---

    /// Create a GPU texture from RGBA8 pixel data.
    /// @param w     Texture width in pixels.
    /// @param h     Texture height in pixels.
    /// @param data  Pointer to w*h*4 bytes of RGBA8 pixel data.
    /// @return Opaque texture handle. Caller owns the returned pointer.
    virtual std::unique_ptr<Texture> createTexture(int w, int h, const void* data) = 0;

    /// Update an existing texture with new RGBA8 pixel data (same dimensions).
    /// @param tex   Texture to update (must not be null).
    /// @param data  Pointer to tex->width * tex->height * 4 bytes.
    virtual void updateTexture(Texture* tex, const void* data) = 0;

    /// Destroy GPU resources associated with a texture.
    /// After this call, tex->id is invalid. The Texture object itself is
    /// still owned by the caller (typically via unique_ptr).
    virtual void destroyTexture(Texture* tex) = 0;

    /// Shut down the texture management subsystem.
    /// Called once at application exit, before shutdownImGui()/shutdown().
    virtual void shutdownTextureSystem() = 0;
```

### 1.3 Replace `VulkanTexture` with `Texture` in `AppState.h`

**File: `include/AppState.h`**

Remove:
```cpp
#include "VulkanHelpers.h"
```

Add (after the existing `#include "Volume.h"`):
```cpp
#include "GraphicsBackend.h"  // for Texture
```

Change `VolumeViewState` (line 19):
```cpp
// Before:
std::unique_ptr<VulkanTexture> sliceTextures[3];
// After:
std::unique_ptr<Texture> sliceTextures[3];
```

Change `OverlayState` (line 36):
```cpp
// Before:
std::unique_ptr<VulkanTexture> textures[3];
// After:
std::unique_ptr<Texture> textures[3];
```

Update the doc comment on `clearAllVolumes()` (line 83-84):
```cpp
/// Clear all volumes, view states, and overlay textures.
/// GPU resources are released via Texture destructor.
```

### 1.4 Thread `GraphicsBackend&` into `ViewManager`

`ViewManager` needs to call `createTexture`/`updateTexture`/`destroyTexture` on the
backend instead of calling `VulkanHelpers` directly.

**File: `include/ViewManager.h`**

Add forward declaration (after `#include "AppState.h"`, before the class):
```cpp
class GraphicsBackend;
```

Change constructor:
```cpp
// Before:
explicit ViewManager(AppState& state);
// After:
ViewManager(AppState& state, GraphicsBackend& backend);
```

Add private member:
```cpp
GraphicsBackend& backend_;
```

**File: `src/ViewManager.cpp`**

Remove:
```cpp
#include "VulkanHelpers.h"
```

Add:
```cpp
#include "GraphicsBackend.h"
```

Change constructor:
```cpp
// Before:
ViewManager::ViewManager(AppState& state) : state_(state) {}
// After:
ViewManager::ViewManager(AppState& state, GraphicsBackend& backend)
    : state_(state), backend_(backend) {}
```

Replace all `VulkanHelpers` calls in `updateSliceTexture()` (lines 108-116):
```cpp
// Before:
std::unique_ptr<VulkanTexture>& tex = state.sliceTextures[viewIndex];
if (!tex)
    tex = VulkanHelpers::CreateTexture(w, h, pixels.data());
else if (tex->width != w || tex->height != h)
{
    VulkanHelpers::DestroyTexture(tex.get());
    tex = VulkanHelpers::CreateTexture(w, h, pixels.data());
}
else
    VulkanHelpers::UpdateTexture(tex.get(), pixels.data());

// After:
std::unique_ptr<Texture>& tex = state.sliceTextures[viewIndex];
if (!tex)
    tex = backend_.createTexture(w, h, pixels.data());
else if (tex->width != w || tex->height != h)
{
    backend_.destroyTexture(tex.get());
    tex = backend_.createTexture(w, h, pixels.data());
}
else
    backend_.updateTexture(tex.get(), pixels.data());
```

Same replacement in `updateOverlayTexture()` (lines 259-267):
```cpp
// Before:
std::unique_ptr<VulkanTexture>& tex = state_.overlay_.textures[viewIndex];
// ... same pattern with VulkanHelpers::CreateTexture/DestroyTexture/UpdateTexture ...

// After:
std::unique_ptr<Texture>& tex = state_.overlay_.textures[viewIndex];
// ... same pattern with backend_.createTexture/destroyTexture/updateTexture ...
```

### 1.5 Fix `Interface.cpp` texture ID access

**File: `src/Interface.cpp`**

At line 975 and 1022 (in `renderSliceView`):
```cpp
// Before:
VulkanTexture* tex = state.sliceTextures[viewIndex].get();
// ...
reinterpret_cast<ImTextureID>(tex->descriptor_set),

// After:
Texture* tex = state.sliceTextures[viewIndex].get();
// ...
tex->id,
```

At line 1341 and 1388 (in `renderOverlayView`):
```cpp
// Before:
VulkanTexture* tex = state_.overlay_.textures[viewIndex].get();
// ...
reinterpret_cast<ImTextureID>(tex->descriptor_set),

// After:
Texture* tex = state_.overlay_.textures[viewIndex].get();
// ...
tex->id,
```

### 1.6 Implement texture methods in `VulkanBackend`

**File: `include/VulkanBackend.h`** — Add method declarations (after `captureScreenshot`):
```cpp
    std::unique_ptr<Texture> createTexture(int w, int h, const void* data) override;
    void updateTexture(Texture* tex, const void* data) override;
    void destroyTexture(Texture* tex) override;
    void shutdownTextureSystem() override;
```

**File: `src/VulkanBackend.cpp`** — Add implementations:

```cpp
std::unique_ptr<Texture> VulkanBackend::createTexture(int w, int h, const void* data)
{
    auto vkTex = VulkanHelpers::CreateTexture(w, h, data);
    if (!vkTex)
        return nullptr;

    auto tex = std::make_unique<Texture>();
    tex->id     = reinterpret_cast<ImTextureID>(vkTex->descriptor_set);
    tex->width  = vkTex->width;
    tex->height = vkTex->height;

    // Store the VulkanTexture pointer in a map keyed by ImTextureID
    // so we can look it up later for update/destroy.
    vulkanTextures_[tex->id] = std::move(vkTex);

    return tex;
}

void VulkanBackend::updateTexture(Texture* tex, const void* data)
{
    if (!tex) return;
    auto it = vulkanTextures_.find(tex->id);
    if (it != vulkanTextures_.end())
        VulkanHelpers::UpdateTexture(it->second.get(), data);
}

void VulkanBackend::destroyTexture(Texture* tex)
{
    if (!tex) return;
    auto it = vulkanTextures_.find(tex->id);
    if (it != vulkanTextures_.end())
    {
        VulkanHelpers::DestroyTexture(it->second.get());
        vulkanTextures_.erase(it);
    }
    tex->id = nullptr;
}

void VulkanBackend::shutdownTextureSystem()
{
    vulkanTextures_.clear();  // ~VulkanTexture() cleans up GPU resources
    VulkanHelpers::Shutdown();
}
```

**File: `include/VulkanBackend.h`** — Add private member:
```cpp
#include <map>
// ...
std::map<ImTextureID, std::unique_ptr<VulkanTexture>> vulkanTextures_;
```

Note: An `unordered_map` would be marginally faster but ImTextureID is a `void*`
which would need a custom hash. `std::map` works fine for the small number of
textures we have (typically < 30).

### 1.7 Clean up `main.cpp`

**File: `src/main.cpp`**

Remove:
```cpp
#define GLFW_INCLUDE_VULKAN    // line 16
#include "VulkanHelpers.h"     // line 26
```

Ensure this remains (already present at line 15):
```cpp
#define GLFW_INCLUDE_NONE
```

Change ViewManager construction (line 306):
```cpp
// Before:
ViewManager viewManager(state);
// After:
ViewManager viewManager(state, *backend);
```

Change shutdown (line 376):
```cpp
// Before:
VulkanHelpers::Shutdown();
// After:
backend->shutdownTextureSystem();
```

### Phase 1 Checkpoint
- `cmake .. && make` succeeds.
- `ctest --output-on-failure` — 11/11 tests pass.
- App runs identically to before (Vulkan only, but no Vulkan types outside backend).
- `grep -r "VulkanTexture\|VulkanHelpers" include/ src/` only matches
  `VulkanBackend.h`, `VulkanBackend.cpp`, `VulkanHelpers.h`, `VulkanHelpers.cpp`.
- **Commit**: "Abstract texture system behind GraphicsBackend interface"

---

## Phase 2: Decouple GLFW Window Hints from Backend ✅ COMPLETE (commit `0d2454a`)

Currently `main.cpp` hardcodes `GLFW_CLIENT_API = GLFW_NO_API` which is Vulkan-specific.
OpenGL needs the default `GLFW_OPENGL_API`. The backend must control this.

### 2.1 Add `setWindowHints()` to `GraphicsBackend` interface

**File: `include/GraphicsBackend.h`** — Add before `initialize()` (around line 24):

```cpp
    /// Set GLFW window hints appropriate for this backend.
    /// Must be called BEFORE glfwCreateWindow().
    /// - Vulkan/Metal: GLFW_CLIENT_API = GLFW_NO_API
    /// - OpenGL: GLFW_CLIENT_API = GLFW_OPENGL_API (default)
    virtual void setWindowHints() = 0;
```

### 2.2 Implement in `VulkanBackend`

**File: `include/VulkanBackend.h`** — Add declaration:
```cpp
void setWindowHints() override;
```

**File: `src/VulkanBackend.cpp`** — Add implementation:
```cpp
void VulkanBackend::setWindowHints()
{
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
}
```

### 2.3 Update `main.cpp`

Move the `glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)` call (line 250) to be called
via the backend:

```cpp
// Before (line 250):
glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

// After:
backend->setWindowHints();
glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
```

This means the backend must be created *before* the GLFW window. Currently (line 298)
the backend is created after the window. We need to reorder:

1. Parse CLI args (including `--backend` flag — added in Phase 3)
2. `glfwInit()`
3. Create backend object (just construction, no GPU init yet)
4. `backend->setWindowHints()`
5. `glfwCreateWindow()`
6. `backend->initialize(window)` (GPU init with the window)

The backend constructor must be lightweight (no GPU work). This is already the case
for `VulkanBackend` (default constructor).

### Phase 2 Checkpoint
- Build + tests pass.
- App runs identically (Vulkan backend sets `GLFW_NO_API` as before).
- **Commit**: "Decouple GLFW window hints from backend via setWindowHints()"

---

## Phase 3: Backend Registry and Selection ✅ COMPLETE (commit `723abc5`)

### 3.1 Define `BackendType` enum

**File: `include/GraphicsBackend.h`** — Add before the `Texture` struct:

```cpp
/// Available graphics backend types.
/// Not all types are available on all platforms — availability depends on
/// compile-time CMake options (ENABLE_VULKAN, ENABLE_OPENGL2, ENABLE_METAL).
enum class BackendType
{
    Vulkan,
    OpenGL2,
    Metal
};
```

### 3.2 Replace factory method

**File: `include/GraphicsBackend.h`** — Replace the existing `createDefault()`:

```cpp
    // --- Factory ---

    /// Create a backend of the specified type.
    /// @throws std::runtime_error if the type was not compiled in.
    static std::unique_ptr<GraphicsBackend> create(BackendType type);

    /// Auto-detect the best available backend for the current platform.
    /// Preference order: Vulkan > OpenGL2 > Metal.
    /// Falls back to the next option if the preferred one is not compiled in.
    static BackendType detectBest();

    /// Return the list of backends that were compiled into this binary.
    static std::vector<BackendType> availableBackends();

    /// Convert a BackendType to a human-readable string.
    static const char* backendName(BackendType type);

    /// Parse a backend name string (case-insensitive).
    /// Returns nullopt if the string is not recognized.
    static std::optional<BackendType> parseBackendName(const std::string& name);
```

### 3.3 CMake compile-time options

**File: `CMakeLists.txt`** — Add after `project()` (around line 3):

```cmake
# --- Backend options ---
option(ENABLE_VULKAN  "Build Vulkan backend"  ON)
option(ENABLE_OPENGL2 "Build OpenGL2 backend" ON)
option(ENABLE_METAL   "Build Metal backend"   OFF)
```

Make Vulkan conditional (replace `find_package(Vulkan REQUIRED)` at line 13):
```cmake
if(ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
endif()

if(ENABLE_OPENGL2)
    find_package(OpenGL REQUIRED)
endif()
```

Conditionally compile backend sources (replace lines 108-113):
```cmake
# ImGui platform backend (always needed)
set(IMGUI_DIR ${imgui_SOURCE_DIR})
list(APPEND SOURCES
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
)

# Backend-specific ImGui renderer sources
if(ENABLE_VULKAN)
    list(APPEND SOURCES ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp)
endif()
if(ENABLE_OPENGL2)
    list(APPEND SOURCES ${IMGUI_DIR}/backends/imgui_impl_opengl2.cpp)
endif()
```

Pass defines to C++ (add after `target_compile_definitions`):
```cmake
if(ENABLE_VULKAN)
    target_compile_definitions(new_register PRIVATE HAS_VULKAN=1)
endif()
if(ENABLE_OPENGL2)
    target_compile_definitions(new_register PRIVATE HAS_OPENGL2=1)
endif()
if(ENABLE_METAL)
    target_compile_definitions(new_register PRIVATE HAS_METAL=1)
endif()
```

Conditionally link (replace `Vulkan::Vulkan` in `target_link_libraries`):
```cmake
target_link_libraries(new_register PRIVATE
    glfw
    imgui
    minc2-simple-static
    ${MINC2_LIB}
    nlohmann_json::nlohmann_json
    glm
)
if(ENABLE_VULKAN)
    target_link_libraries(new_register PRIVATE Vulkan::Vulkan)
endif()
if(ENABLE_OPENGL2)
    target_link_libraries(new_register PRIVATE OpenGL::GL)
endif()
```

Conditionally compile backend source files. The `file(GLOB_RECURSE)` at line 102
picks up all `.cpp` in `src/`. We need to exclude backend files when their backend
is disabled. Two approaches:

**Option A (recommended):** Switch from GLOB to explicit source list. This is better
CMake practice anyway:
```cmake
set(SOURCES
    src/main.cpp
    src/AppState.cpp
    src/AppConfig.cpp
    src/ColourMap.cpp
    src/Interface.cpp
    src/QCState.cpp
    src/TagWrapper.cpp
    src/ViewManager.cpp
    src/Volume.cpp
)
if(ENABLE_VULKAN)
    list(APPEND SOURCES src/VulkanBackend.cpp src/VulkanHelpers.cpp)
endif()
if(ENABLE_OPENGL2)
    list(APPEND SOURCES src/OpenGL2Backend.cpp)
endif()
```

**Option B:** Keep GLOB but exclude with `list(REMOVE_ITEM)`. Less clean.

Go with **Option A**.

### 3.4 Implement factory in a new file `src/BackendFactory.cpp`

This avoids putting `#ifdef` logic inside any single backend file. The factory
implementation needs to know about all compiled-in backends.

**File: `src/BackendFactory.cpp`** (new):

```cpp
#include "GraphicsBackend.h"
#include <stdexcept>
#include <algorithm>
#include <cctype>

#ifdef HAS_VULKAN
#include "VulkanBackend.h"
#endif
#ifdef HAS_OPENGL2
#include "OpenGL2Backend.h"
#endif

std::unique_ptr<GraphicsBackend> GraphicsBackend::create(BackendType type)
{
    switch (type)
    {
#ifdef HAS_VULKAN
    case BackendType::Vulkan:
        return std::make_unique<VulkanBackend>();
#endif
#ifdef HAS_OPENGL2
    case BackendType::OpenGL2:
        return std::make_unique<OpenGL2Backend>();
#endif
    default:
        throw std::runtime_error(
            std::string("Backend not available: ") + backendName(type));
    }
}

BackendType GraphicsBackend::detectBest()
{
    auto avail = availableBackends();
    if (avail.empty())
        throw std::runtime_error("No graphics backends compiled in");
    return avail.front();
}

std::vector<BackendType> GraphicsBackend::availableBackends()
{
    std::vector<BackendType> result;
#ifdef HAS_VULKAN
    result.push_back(BackendType::Vulkan);
#endif
#ifdef HAS_OPENGL2
    result.push_back(BackendType::OpenGL2);
#endif
#ifdef HAS_METAL
    result.push_back(BackendType::Metal);
#endif
    return result;
}

const char* GraphicsBackend::backendName(BackendType type)
{
    switch (type)
    {
    case BackendType::Vulkan:  return "vulkan";
    case BackendType::OpenGL2: return "opengl2";
    case BackendType::Metal:   return "metal";
    }
    return "unknown";
}

std::optional<BackendType> GraphicsBackend::parseBackendName(const std::string& name)
{
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return std::tolower(c); });

    if (lower == "vulkan" || lower == "vk")    return BackendType::Vulkan;
    if (lower == "opengl2" || lower == "gl2"
        || lower == "opengl" || lower == "gl") return BackendType::OpenGL2;
    if (lower == "metal" || lower == "mtl")    return BackendType::Metal;
    return std::nullopt;
}
```

Add `BackendFactory.cpp` to the explicit source list in CMake (always compiled).

### 3.5 Remove old `createDefault()` from `VulkanBackend.cpp`

**File: `src/VulkanBackend.cpp`** — Delete lines 39-42:
```cpp
std::unique_ptr<GraphicsBackend> GraphicsBackend::createDefault()
{
    return std::make_unique<VulkanBackend>();
}
```

This is now handled by `BackendFactory.cpp`.

### 3.6 Add `--backend` CLI flag to `main.cpp`

**File: `src/main.cpp`** — Add to arg parsing loop:

```cpp
else if ((arg == "--backend" || arg == "-B") && i + 1 < argc)
{
    cliBackendName = argv[++i];
}
```

After arg parsing, determine backend type:
```cpp
BackendType backendType;
if (!cliBackendName.empty())
{
    if (cliBackendName == "auto")
    {
        backendType = GraphicsBackend::detectBest();
    }
    else
    {
        auto parsed = GraphicsBackend::parseBackendName(cliBackendName);
        if (!parsed)
        {
            std::cerr << "Unknown backend: " << cliBackendName << "\n";
            std::cerr << "Available:";
            for (auto b : GraphicsBackend::availableBackends())
                std::cerr << " " << GraphicsBackend::backendName(b);
            std::cerr << "\n";
            return 1;
        }
        backendType = *parsed;
    }
}
else
{
    backendType = GraphicsBackend::detectBest();
}
```

Create backend before window:
```cpp
auto backend = GraphicsBackend::create(backendType);
backend->setWindowHints();
glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

GLFWwindow* window = glfwCreateWindow(initW, initH,
    "New Register", nullptr, nullptr);
```

Update the window title to include backend name:
```cpp
std::string title = std::string("New Register (") +
    GraphicsBackend::backendName(backendType) + ")";
GLFWwindow* window = glfwCreateWindow(initW, initH,
    title.c_str(), nullptr, nullptr);
```

Update help text:
```
  -B, --backend <name>   Graphics backend: auto, vulkan, opengl2 (default: auto)
```

### 3.7 Runtime fallback on initialization failure

In `main.cpp`, wrap `backend->initialize(window)` with a fallback:

```cpp
try
{
    backend->initialize(window);
}
catch (const std::exception& e)
{
    std::cerr << "[backend] " << GraphicsBackend::backendName(backendType)
              << " failed: " << e.what() << "\n";

    // Try fallback backends
    bool initialized = false;
    for (auto fallback : GraphicsBackend::availableBackends())
    {
        if (fallback == backendType)
            continue;
        std::cerr << "[backend] Trying fallback: "
                  << GraphicsBackend::backendName(fallback) << "\n";
        try
        {
            glfwDestroyWindow(window);
            backend = GraphicsBackend::create(fallback);
            backend->setWindowHints();
            glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
            window = glfwCreateWindow(initW, initH,
                (std::string("New Register (") +
                 GraphicsBackend::backendName(fallback) + ")").c_str(),
                nullptr, nullptr);
            if (!window)
                continue;
            backend->initialize(window);
            backendType = fallback;
            initialized = true;
            break;
        }
        catch (const std::exception& e2)
        {
            std::cerr << "[backend] " << GraphicsBackend::backendName(fallback)
                      << " also failed: " << e2.what() << "\n";
        }
    }
    if (!initialized)
    {
        std::cerr << "Error: No usable graphics backend found.\n";
        glfwTerminate();
        return 1;
    }
}
```

### Phase 3 Checkpoint
- Build with `-DENABLE_VULKAN=ON -DENABLE_OPENGL2=ON` succeeds (even though
  `OpenGL2Backend.cpp` doesn't exist yet — it won't be in the source list until
  Phase 4). Actually at this point we need at least a stub. See Phase 4.
- Build with `-DENABLE_VULKAN=ON -DENABLE_OPENGL2=OFF` succeeds.
- `--backend vulkan` works, `--backend opengl2` gives "Backend not available"
  (until Phase 4).
- 11/11 tests pass.
- **Commit**: "Add backend registry, factory, CLI selection, and runtime fallback"

---

## Phase 4: Implement OpenGL2Backend ✅ COMPLETE (commit `723abc5`)

### 4.1 Create `include/OpenGL2Backend.h`

```cpp
#pragma once

#include "GraphicsBackend.h"

#include <map>

struct GLFWwindow;

/// OpenGL 2 (fixed-function pipeline) backend.
/// Uses ImGui's imgui_impl_opengl2 renderer backend.
/// This is the simplest backend, suitable for legacy Linux systems,
/// software renderers (Mesa llvmpipe), and SSH/X11 forwarding.
class OpenGL2Backend : public GraphicsBackend
{
public:
    OpenGL2Backend() = default;
    ~OpenGL2Backend() override = default;

    // --- Lifecycle ---
    void setWindowHints() override;
    void initialize(GLFWwindow* window) override;
    void shutdown() override;
    void waitIdle() override;

    // --- Frame cycle ---
    bool needsSwapchainRebuild() const override;
    void rebuildSwapchain(int width, int height) override;
    void beginFrame() override;
    void endFrame() override;

    // --- ImGui integration ---
    void initImGui(GLFWwindow* window) override;
    void shutdownImGui() override;
    void imguiNewFrame() override;
    void imguiRenderDrawData() override;

    // --- DPI ---
    float contentScale() const override;

    // --- Screenshot ---
    std::vector<uint8_t> captureScreenshot(int& width, int& height) override;

    // --- Texture management ---
    std::unique_ptr<Texture> createTexture(int w, int h, const void* data) override;
    void updateTexture(Texture* tex, const void* data) override;
    void destroyTexture(Texture* tex) override;
    void shutdownTextureSystem() override;

private:
    GLFWwindow* window_ = nullptr;
    float contentScale_ = 1.0f;
    int fbWidth_ = 0;
    int fbHeight_ = 0;

    /// Map from ImTextureID to OpenGL texture name (GLuint), for cleanup.
    std::map<ImTextureID, unsigned int> glTextures_;
};
```

### 4.2 Create `src/OpenGL2Backend.cpp`

This is the core new implementation. OpenGL 2 is refreshingly simple compared to
Vulkan — no instances, no devices, no command buffers, no descriptor sets, no
swapchains. GLFW manages the GL context, and buffer swapping is just
`glfwSwapBuffers()`.

```cpp
#include "OpenGL2Backend.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl2.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// We need GL headers for texture management and glReadPixels.
// On Linux, GLFW's built-in GL loader or a system header works.
// imgui_impl_opengl2.cpp uses its own minimal loader, but we need
// a few functions directly. Use GLFW's built-in loader for simplicity.
#ifdef _WIN32
#include <GL/gl.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#endif

#include <iostream>
#include <stdexcept>
#include <cstring>

// ---------------------------------------------------------------------------
// GLFW hints
// ---------------------------------------------------------------------------

void OpenGL2Backend::setWindowHints()
{
    // Use default GLFW_OPENGL_API (no hint needed — it's the default).
    // Explicitly reset in case a previous backend attempt set GLFW_NO_API.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    // Do NOT request core profile — GL 2.1 uses compatibility profile.
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void OpenGL2Backend::initialize(GLFWwindow* window)
{
    window_ = window;

    // OpenGL requires making the context current before any GL calls.
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // VSync

    // Query content scale for HiDPI
    {
        float xscale = 1.0f, yscale = 1.0f;
        glfwGetWindowContentScale(window, &xscale, &yscale);
        contentScale_ = (xscale > yscale) ? xscale : yscale;
        if (contentScale_ < 1.0f) contentScale_ = 1.0f;
    }

    // Store initial framebuffer size
    glfwGetFramebufferSize(window, &fbWidth_, &fbHeight_);

    std::cerr << "[opengl2] Initialized: " << glGetString(GL_RENDERER)
              << " (" << glGetString(GL_VERSION) << ")\n";
}

void OpenGL2Backend::shutdown()
{
    // OpenGL context is destroyed when the GLFW window is destroyed.
    // Nothing to do here.
}

void OpenGL2Backend::waitIdle()
{
    glFinish();
}

// ---------------------------------------------------------------------------
// Frame cycle
// ---------------------------------------------------------------------------

bool OpenGL2Backend::needsSwapchainRebuild() const
{
    // OpenGL handles resize automatically — the default framebuffer
    // always matches the window. No explicit "swapchain rebuild" needed.
    return false;
}

void OpenGL2Backend::rebuildSwapchain(int width, int height)
{
    fbWidth_ = width;
    fbHeight_ = height;
    glViewport(0, 0, width, height);
}

void OpenGL2Backend::beginFrame()
{
    // Update framebuffer size each frame (handles resize)
    glfwGetFramebufferSize(window_, &fbWidth_, &fbHeight_);
    glViewport(0, 0, fbWidth_, fbHeight_);
}

void OpenGL2Backend::endFrame()
{
    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData) return;

    glViewport(0, 0,
        static_cast<int>(drawData->DisplaySize.x),
        static_cast<int>(drawData->DisplaySize.y));
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL2_RenderDrawData(drawData);

    glfwSwapBuffers(window_);
}

// ---------------------------------------------------------------------------
// ImGui integration
// ---------------------------------------------------------------------------

void OpenGL2Backend::initImGui(GLFWwindow* window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    // Scale the entire ImGui style for HiDPI
    if (contentScale_ > 1.0f)
        ImGui::GetStyle().ScaleAllSizes(contentScale_);

    // Load default font at scaled size
    {
        float fontSize = 13.0f * contentScale_;
        ImFontConfig fontCfg;
        fontCfg.SizePixels = fontSize;
        fontCfg.OversampleH = 1;
        fontCfg.OversampleV = 1;
        fontCfg.PixelSnapH = true;
        io.Fonts->AddFontDefault(&fontCfg);
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();
}

void OpenGL2Backend::shutdownImGui()
{
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void OpenGL2Backend::imguiNewFrame()
{
    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplGlfw_NewFrame();
}

void OpenGL2Backend::imguiRenderDrawData()
{
    endFrame();
}

float OpenGL2Backend::contentScale() const
{
    return contentScale_;
}

// ---------------------------------------------------------------------------
// Screenshot
// ---------------------------------------------------------------------------

std::vector<uint8_t> OpenGL2Backend::captureScreenshot(int& width, int& height)
{
    glFinish();

    glfwGetFramebufferSize(window_, &width, &height);
    if (width <= 0 || height <= 0)
        return {};

    std::vector<uint8_t> pixels(width * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // OpenGL reads bottom-to-top; flip vertically for top-to-bottom RGBA.
    int rowBytes = width * 4;
    std::vector<uint8_t> rowBuf(rowBytes);
    for (int y = 0; y < height / 2; ++y)
    {
        uint8_t* top = pixels.data() + y * rowBytes;
        uint8_t* bot = pixels.data() + (height - 1 - y) * rowBytes;
        std::memcpy(rowBuf.data(), top, rowBytes);
        std::memcpy(top, bot, rowBytes);
        std::memcpy(bot, rowBuf.data(), rowBytes);
    }

    return pixels;
}

// ---------------------------------------------------------------------------
// Texture management
// ---------------------------------------------------------------------------

std::unique_ptr<Texture> OpenGL2Backend::createTexture(int w, int h, const void* data)
{
    GLuint texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    auto tex = std::make_unique<Texture>();
    tex->id     = reinterpret_cast<ImTextureID>(static_cast<intptr_t>(texId));
    tex->width  = w;
    tex->height = h;

    glTextures_[tex->id] = texId;
    return tex;
}

void OpenGL2Backend::updateTexture(Texture* tex, const void* data)
{
    if (!tex) return;
    auto it = glTextures_.find(tex->id);
    if (it == glTextures_.end()) return;

    glBindTexture(GL_TEXTURE_2D, it->second);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex->width, tex->height,
                    GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGL2Backend::destroyTexture(Texture* tex)
{
    if (!tex) return;
    auto it = glTextures_.find(tex->id);
    if (it != glTextures_.end())
    {
        glDeleteTextures(1, &it->second);
        glTextures_.erase(it);
    }
    tex->id = nullptr;
}

void OpenGL2Backend::shutdownTextureSystem()
{
    for (auto& [id, glId] : glTextures_)
        glDeleteTextures(1, &glId);
    glTextures_.clear();
}
```

### 4.3 Update CMakeLists.txt

Add `OpenGL2Backend.cpp` to the conditional source list (inside `if(ENABLE_OPENGL2)`).
Add `BackendFactory.cpp` to the always-compiled source list.

### Phase 4 Checkpoint
- Build with both backends: `cmake -DENABLE_VULKAN=ON -DENABLE_OPENGL2=ON ..`
- `--backend vulkan` works as before.
- `--backend opengl2` launches with OpenGL 2 and renders correctly.
- `--backend auto` picks Vulkan first (if available), falls back to OpenGL2.
- 11/11 tests pass.
- Screenshot works with both backends.
- **Commit**: "Implement OpenGL 2 backend"

---

## Phase 5: Polish and Metal Stub — IN PROGRESS

### 5.1 Diagnostic logging at startup

Print the selected backend and available backends:
```cpp
std::cerr << "[backend] Using: " << GraphicsBackend::backendName(backendType) << "\n";
std::cerr << "[backend] Available:";
for (auto b : GraphicsBackend::availableBackends())
    std::cerr << " " << GraphicsBackend::backendName(b);
std::cerr << "\n";
```

### 5.2 Update window title dynamically

After successful backend init:
```cpp
std::string title = std::string("New Register (") +
    GraphicsBackend::backendName(backendType) + ")";
glfwSetWindowTitle(window, title.c_str());
```

This handles the fallback case where the actual backend differs from the originally
requested one.

### 5.3 Metal documentation in PLAN.md

Add a "Metal Backend (Future)" section to PLAN.md documenting what's needed:

1. `include/MetalBackend.h` / `src/MetalBackend.mm` (Objective-C++)
2. CMake: `enable_language(OBJCXX)`, find Metal framework
3. GLFW: `GLFW_NO_API`, create `CAMetalLayer` on native Cocoa view
4. ImGui: `ImGui_ImplGlfw_InitForOther()` + `ImGui_ImplMetal_Init(device)`
5. Texture: `MTLTexture` objects, `ImTextureID` = `MTLTexture*`

### 5.4 OpenGL2 missing features check

The OpenGL2 backend uses `glReadPixels` for screenshots which reads the front buffer
after swap. This might produce a black frame on some drivers. If so, read before
`glfwSwapBuffers()` instead. Test and fix if needed.

### Phase 5 Checkpoint
- All polish applied.
- Both backends work correctly.
- 11/11 tests pass.
- **Commit**: "Polish backend selection, add Metal stub documentation"

---

## Implementation Order and Checkpoints

| Step | Phase | Files Changed / Added | Commit |
|---|---|---|---|
| 1 | 1.1–1.7 | `GraphicsBackend.h`, `VulkanBackend.h`, `VulkanBackend.cpp`, `AppState.h`, `ViewManager.h`, `ViewManager.cpp`, `Interface.cpp`, `main.cpp` | `26df1fb` ✅ |
| 2 | 2.1–2.3 | `GraphicsBackend.h`, `VulkanBackend.h`, `VulkanBackend.cpp`, `main.cpp` | `0d2454a` ✅ |
| 3 | 3.1–3.7 | `GraphicsBackend.h`, `CMakeLists.txt`, `BackendFactory.cpp` (new), `VulkanBackend.cpp`, `main.cpp` | `723abc5` ✅ |
| 4 | 4.1–4.3 | `OpenGL2Backend.h` (new), `OpenGL2Backend.cpp` (new), `CMakeLists.txt` | `723abc5` ✅ |
| 5 | 5.1–5.4 | `main.cpp`, `PLAN.md`, `README.md` | In progress |

Each step should be a separate commit. The app compiles and runs (Vulkan only)
after steps 1-3. OpenGL2 becomes functional at step 4.

---

## File Changes Summary

| File | Action | Phase | Description |
|---|---|---|---|
| `include/GraphicsBackend.h` | Edit | 1, 2, 3 | Add `Texture` struct, texture methods, `setWindowHints()`, `BackendType` enum, factory methods |
| `include/VulkanBackend.h` | Edit | 1, 2 | Add texture method overrides, `vulkanTextures_` map, `setWindowHints()` |
| `src/VulkanBackend.cpp` | Edit | 1, 2, 3 | Implement texture methods, `setWindowHints()`, remove old `createDefault()` |
| `include/AppState.h` | Edit | 1 | Replace `VulkanTexture` with `Texture`, remove `VulkanHelpers.h` include |
| `include/ViewManager.h` | Edit | 1 | Add `GraphicsBackend&` to constructor |
| `src/ViewManager.cpp` | Edit | 1 | Replace `VulkanHelpers` calls with `backend_` calls |
| `src/Interface.cpp` | Edit | 1 | Replace `VulkanTexture*` with `Texture*`, `descriptor_set` with `id` |
| `src/main.cpp` | Edit | 1, 2, 3, 5 | Remove Vulkan includes, add `--backend`, reorder init, add fallback |
| `src/BackendFactory.cpp` | **New** | 3 | Factory method, `detectBest()`, `availableBackends()`, name parsing |
| `include/OpenGL2Backend.h` | **New** | 4 | OpenGL 2 backend header |
| `src/OpenGL2Backend.cpp` | **New** | 4 | Full OpenGL 2 backend implementation |
| `CMakeLists.txt` | Edit | 3, 4 | Backend options, conditional compilation and linking |
| `PLAN.md` | Edit | 5 | Multi-backend section, Metal stub documentation |

---

## Risk Assessment

- **Phase 1 (Texture Abstraction)** is the highest risk — it touches the most files
  and changes the texture ownership model. But it's a clean mechanical refactor with
  clear before/after states. The `vulkanTextures_` map in `VulkanBackend` adds one
  indirection layer but the texture count is small (< 30).

- **Phase 2 (GLFW Decoupling)** requires reordering initialization in `main.cpp`.
  The key constraint is that backend construction must be lightweight (no GPU work)
  and `setWindowHints()` must be called before `glfwCreateWindow()`. Both are easy
  to verify.

- **Phase 3 (Registry)** is straightforward plumbing. The fallback logic in `main.cpp`
  is the most complex part (destroy window, recreate with new hints).

- **Phase 4 (OpenGL2)** is the most new code but also the simplest conceptually.
  OpenGL 2 is trivial compared to Vulkan — ImGui's `imgui_impl_opengl2` does all
  the heavy lifting. The texture management is 4 GL calls per operation.

- **OpenGL header portability**: On Linux `<GL/gl.h>` is standard. On macOS it would
  be `<OpenGL/gl.h>`. The `#ifdef` in the header section handles this. Metal backend
  (future) has its own headers entirely.
