# macOS Port — Progress & Handoff

**Goal:** make `new_register` (and eventually `new_qc`) build and run on the latest
macOS. Development happens on Linux; final compile/run is on the user's Mac.

**Chosen approach (decided with the user):** a *native* macOS path using
**`imgui_impl_osx`** (input/platform) + **`imgui_impl_metal`** (renderer). On macOS the
app uses native `NSApplication`/`NSWindow`/`MTKView` and does **not** link GLFW, Vulkan,
or OpenGL. This is NOT MoltenVK and NOT GLFW+Metal.

**Plan file:** `/home/vfonov/.claude/plans/carefully-review-new-register-project-eager-mitten.md`

---

## Status at end of last session

### `new_register` — DONE (Linux-verified parts) + Mac code written (not yet compiled)

| Phase | What | State |
|------|------|-------|
| 1 | Decouple shared UI from GLFW | ✅ done, builds + all 27 ctests pass on Linux |
| 2 | CMake macOS scaffolding (`if(APPLE)`) | ✅ done, non-APPLE build unaffected |
| 3 | `MetalBackend.mm/.h` | ✅ written (compiles on Mac only) |
| 4 | `main_macos.mm` native loop | ✅ written (compiles on Mac only) |
| 5 | Docs + `new_qc` gated off on APPLE | ✅ done |

**Linux build is green after every change** — `cd new_register/build && cmake .. &&
make -j$(nproc) && ctest` → `100% tests passed, 0 tests failed out of 27`.

### `new_qc` — NOT STARTED on macOS

Currently **disabled on APPLE** via `BUILD_NEW_QC=OFF` default (see
`CMakeLists.txt` `_DEFAULT_BUILD_NEW_QC`). The whole next session is about porting it.
Detailed plan below in **"Next: porting `new_qc` to macOS"**.

---

## What changed in `new_register` (file-by-file)

### New files
- `include/CliArgs.h`, `src/CliArgs.cpp` — CLI parser (`ParsedArgs`, `PerVolOpts`,
  `parseArgs`, `printUsage`) extracted out of `main.cpp` into `nr_core` so both
  `main.cpp` (GLFW) and `main_macos.mm` (Cocoa) share it. **Linux-verified.**
- `include/MetalBackend.h` — pure-C++ PIMPL header (no ObjC types) so `BackendFactory.cpp`
  can include it. **Mac-compiled.**
- `src/MetalBackend.mm` — `GraphicsBackend` impl via `imgui_impl_metal`. ARC. **Mac-compiled.**
- `src/main_macos.mm` — native `NSApp`/`NSWindow`/`MTKView` + `imgui_impl_osx`; mirrors
  `main.cpp`'s setup. ARC. **Mac-compiled.**

### Edited (Linux-verified)
- `include/GraphicsBackend.h` — added pure virtuals `requestClose()` and
  `windowSize(int&,int&)`. This is what lets `Interface` avoid touching GLFW.
- `src/VulkanBackend.cpp` + `.h`, `src/OpenGL2Backend.cpp` + `.h` — implement the two new
  methods against the stored `window_` (`glfwSetWindowShouldClose`/`glfwGetWindowSize`).
- `include/Interface.h`, `src/Interface.cpp` — dropped the `GLFWwindow*` params from
  `render()`/`renderToolsPanel()`; removed `#include <GLFW/glfw3.h>`; replaced the member
  `GLFWwindow* interfaceWindow_` with `GraphicsBackend* backend_` (set at top of
  `render()`); the 2 quit calls now use `backend.requestClose()`, the 2 window-size reads
  use `backend_->windowSize(...)`.
- `src/main.cpp` — removed the inlined CLI parser (now in CliArgs); updated
  `interface.render(*backend)` call; added `#include "CliArgs.h"`.
- `src/BackendFactory.cpp` — added `#ifdef HAS_METAL` case returning `MetalBackend`.
- `CMakeLists.txt` — `if(APPLE)` platform block: `enable_language(OBJCXX)`; per-platform
  defaults (`_DEFAULT_ENABLE_VULKAN/OPENGL2/METAL/TESTS/BUILD_NEW_QC`); `.dylib`/`.tbd`
  library suffixes; on APPLE the `new_register` target compiles `main_macos.mm` +
  `MetalBackend.mm` (instead of `main.cpp`/`WindowManager.cpp`/Vulkan/GL backends) and
  `imgui_impl_osx.mm` + `imgui_impl_metal.mm` (instead of `imgui_impl_glfw.cpp`); links
  `-framework Metal/MetalKit/QuartzCore/AppKit/Foundation/GameController`, no GLFW; ARC
  set on the two `.mm` files; `CliArgs.cpp` added to `nr_core`.
- `cmake-modules/FindNETCDF.cmake` — added Homebrew search roots (`/opt/homebrew/...`).
- `src/OpenGL2Backend.cpp`, `src/qc/OpenGL2Backend.cpp` — defensive `<OpenGL/gl.h>` on APPLE.
- `README.md`, `AGENTS.md` — macOS build sections.

### Key design notes / contracts (read before editing)
- **`GraphicsBackend` interface is unchanged in shape for Metal**: `setWindowHints()` and
  `initialize(GLFWwindow*)`/`initImGui(GLFWwindow*)` are no-ops on Metal; the native view
  is supplied via the Metal-only method `MetalBackend::setNativeView(void* mtkView)` before
  `initialize`. `main_macos.mm` constructs `MetalBackend` directly (not via the factory).
- **DPI:** `MetalBackend` sets `framebufferScale_ == contentScale_` so `imguiScale()==1.0`
  by default; Retina sharpness comes from `imgui_impl_osx` setting `DisplayFramebufferScale`.
  `--scale` still overrides via `setContentScale()`. (Do not manually upscale style+font on
  Retina or it double-scales.)
- **Screenshot:** `MetalBackend::endFrame()` blits the drawable into a persistent managed
  `captureTexture` every frame; `captureScreenshot()` synchronizes + `getBytes` + swizzles
  BGRA→RGBA. No vertical flip (Metal origin is top-left).
- **Textures** are held in an ObjC `NSMutableDictionary` keyed by the `ImTextureID` pointer
  value (ARC keeps them alive); `ImTextureID = (uintptr_t)(__bridge void*)mtlTexture`.
- **Hotkeys in `new_register` already go through ImGui IO** (`ImGui::IsKeyPressed`), so
  `imgui_impl_osx` covers them with no per-key translation. (Contrast with `new_qc`, below.)

---

## Remaining `new_register` work (Mac-side, user)

1. `xcode-select --install`; `brew install hdf5 netcdf`.
2. `cd new_register/build && cmake .. && make -j$(sysctl -n hw.ncpu)`.
3. Expect 1–2 rounds of fix-ups in `MetalBackend.mm` / `main_macos.mm` (only files never
   compiled here). All C++ API calls in `main_macos.mm` were verified against headers.
4. Validate: window opens, slices render, crosshair/colourmaps/overlay/zoom/pan work,
   `P` screenshot writes a correct PNG, Retina scaling looks right.

**Top risks:** (a) `libminc`/`minc2-simple` ExternalProjects building vs brew HDF5/NetCDF;
(b) `imgui_impl_osx` event coverage; (c) Retina/DPI parity.

---

## NEXT: porting `new_qc` to macOS

`new_qc` is a **separate** lightweight image-QC viewer under `new_register/src/qc/`. It does
**not** use MINC/HDF5 (loads PNG/JPG via stb_image), has its **own** backend abstraction,
and its own app/loop class `QC::QCApp`. It is currently `BUILD_NEW_QC=OFF` on APPLE.

### Why it's a separate, slightly harder job than `new_register`
`new_qc` has its own `Backend` base class (`src/qc/Backend.h`) — distinct from
`GraphicsBackend`, with `enum class BackendType { Vulkan, OpenGL2 }` (no Metal) — and
`QCApp` is **much more GLFW-entangled** than `new_register`'s `Interface`:
`QCApp` owns `glfwInit`, window creation, the run loop (`QCApp::run()`), and **GLFW key
callbacks** (`glfwKeyCallback` → `handleKeyboard(key, scancode, action, mods)` using
`GLFW_KEY_*` codes). So the keyboard shortcuts (P=pass, F=fail, ←/→ nav, Ctrl+S save,
Esc exit) are **GLFW-event-driven, not ImGui-IO-driven** — the single biggest thing to
address for a native path.

### Source map (`src/qc/`)
- `Backend.h` — abstract backend interface (mirror of `GraphicsBackend`, fewer methods:
  **no** `requestClose`/`windowSize`). `BackendType{Vulkan,OpenGL2}`.
- `BackendFactory.cpp` — `Backend::create/detectBest/parseBackendName` + X11 detection.
- `VulkanBackend.{cpp,h}`, `VulkanHelpers.{cpp,h}`, `OpenGL2Backend.{cpp,h}` — concrete backends.
- `QCApp.{cpp,h}` — owns GLFW window + event callbacks + `run()` loop + `renderUI()` +
  `handleKeyboard()` + image load/prefetch.
- `CSVHandler.{cpp,h}` — input/output CSV (id,visit,picture → +QC,notes).
- `main.cpp` — arg parse → `QCApp app; app.init(...); app.run(); app.shutdown();`.

### Recommended plan (Phases QC-1 … QC-4)

**QC-1 — Make keyboard handling platform-independent (do FIRST; Linux-verifiable).**
Refactor `QCApp::handleKeyboard(GLFW key,...)` to instead poll ImGui IO inside
`renderUI()`/each frame (`ImGui::IsKeyPressed(ImGuiKey_P)`, `_F`, `_LeftArrow`,
`_RightArrow`, `_PageUp`, `_PageDown`, `ImGui::GetIO().KeyCtrl && IsKeyPressed(_S)`,
`_Escape`). Remove `glfwSetKeyCallback`/`glfwKeyCallback`/`handleKeyboard(int,...)`.
This (a) makes input work automatically under both `imgui_impl_glfw` and `imgui_impl_osx`,
and (b) is fully testable on Linux (the GLFW build still works). Mirror exactly how
`new_register/Interface.cpp` reads hotkeys. **Verify on Linux: build + run `new_qc`.**
Update the keyboard list in `new_qc`'s on-screen help / README if any binding shifts.

**QC-2 — Add Metal to the qc `Backend` abstraction (mostly Linux-verifiable for the C++ side).**
- Add `Metal` to `src/qc/Backend.h`'s `BackendType`; add `"metal"/"mtl"` to
  `parseBackendName`; add `Metal` to `availableBackends`/`detectBest` under `#ifdef HAS_METAL`.
- Create `src/qc/MetalBackend.mm` + `.h` implementing the qc `Backend` interface. This is a
  near-copy of `new_register/src/MetalBackend.mm` MINUS `requestClose`/`windowSize` (not in
  the qc `Backend` base) and using the qc `Texture`/`Backend` types. Reuse the same PIMPL +
  ARC + capture-texture-screenshot pattern. Add a Metal-only `setNativeView(void*)`.
- Add the `#ifdef HAS_METAL` case to `src/qc/BackendFactory.cpp`'s `Backend::create`.

**QC-3 — Native window/loop for `new_qc` (Mac-only).**
Two options — recommend **(A)**:
- **(A) `QCApp_macos.mm`** — a native `NSApplication`/`NSWindow`/`MTKView` driver that
  reuses `QCApp`'s `init()`/`renderUI()`/image logic but replaces `run()`'s GLFW loop with
  an `MTKViewDelegate drawInMTKView:` that calls `imguiNewFrame → NewFrame → renderUI →
  Render → endFrame`. Needs `QCApp` to expose `renderUI()` + state (make them accessible,
  or add a `QCApp::renderFrame(Backend&)` entry). Construct `qc::MetalBackend`, `setNativeView`,
  `initImGui`. Cleanup mirrors `QCApp::shutdown()` (minus GLFW). Mirror
  `new_register/src/main_macos.mm` closely — it's the proven template.
  - In `CMakeLists.txt`, the `new_qc` target's APPLE branch compiles `QCApp_macos.mm` +
    `src/qc/MetalBackend.mm` + `imgui_impl_osx.mm`/`imgui_impl_metal.mm` instead of
    `QCApp.cpp`'s GLFW loop bits + GLFW/Vulkan/GL backends; link the Metal frameworks; ARC
    on the `.mm` files; no GLFW.
- **(B)** Decouple `QCApp` from GLFW entirely behind a small window abstraction (like the
  `new_register` Phase 1 refactor). Cleaner long-term but a bigger change to `QCApp`.

**QC-4 — Re-enable + validate.**
- Flip `_DEFAULT_BUILD_NEW_QC` to `ON` on APPLE (CMakeLists.txt), or make it conditional on
  the qc Metal path existing.
- Build on Mac; run `./new_qc input.csv output.csv`; verify P/F/arrows/Ctrl+S/Esc, image
  display, scaling, CSV round-trip.

### `new_qc` CMake landmarks (current, in `new_register/CMakeLists.txt`)
- `set(QC_SOURCES …)` ~line 632; `add_executable(new_qc …)` ~line 664; backend
  `#ifdef`/link blocks between them and below; `if(WIN32) … user32 shell32` ~line 702.
  Mirror the `new_register` APPLE pattern already applied at the `new_register` target
  block (`add_executable(new_register …)` ~line 535, with the source/link/ARC `if(APPLE)`
  branches just above and below it). Line numbers drift as the file is edited — grep for
  the anchors rather than trusting the numbers.

### Gotchas specific to `new_qc`
- It has its **own** `debugLoggingEnabled()` (inline in `Backend.h`, always false) and its
  **own** `Texture`/`Backend` types — don't accidentally pull in `new_register`'s
  `GraphicsBackend.h`. Keep the qc Metal backend against `qc/Backend.h`.
- `os_prefetch_file` (OsPrefetch.h) is already a no-op on non-Linux — fine on macOS.
- `new_qc` does not need HDF5/NetCDF; the macOS deps are just Xcode CLT (Metal) — it can be
  built even on a Mac without the MINC stack (`BUILD_QC_ONLY=ON` path).

---

## Quick verification commands

```bash
# Linux (regression — must stay green through QC-1/QC-2 C++ work):
cd new_register/build && cmake .. && make -j$(nproc) && ctest --output-on-failure

# macOS (final):
xcode-select --install
brew install hdf5 netcdf            # omit for BUILD_QC_ONLY new_qc-only builds
cd new_register/build && cmake .. && make -j$(sysctl -n hw.ncpu)
./new_register ../test_data/<vol>.mnc
./new_qc input.csv output.csv      # after QC-1..QC-4
```
