# AGENTS.md

> **Note to Agents:** This file contains instructions for operating within this repository. Please read it carefully before making changes.

## 1. Project Overview & Environment

- **Stack:** C/C++ (Legacy codebase), CMake, Autotools (configure/make)
- **Legacy code:** 
  - `register`: Volume registration tool (application), this is the code we are re-implementing
  - `bicgl`: BIC Graphics Library (internal) - also will be reimplemented in the new version
  - `bicpl`: BIC Programming Library (dependency) - will try not to use any functionality from here
  - `libminc`: Medical Imaging NetCDF (dependency), medial image IO library
- **Structure:**
  - `legacy/bicgl/`: **BIC Graphics Library** source code.
    - `OpenGL_graphics/`: OpenGL implementation.
    - `GLUT_windows/`: GLUT windowing system integration.
    - `Testing/`: Test suite for graphics functions.
  - `legacy/register/`: **Register** application source code.
    - `User_interface/`: UI components and event handling.
    - `Functionality/`: Core logic for slicing, tagging, and volume management.
- **Config Files:**
  - `CMakeLists.txt` (located in subdirectories `legacy/bicgl`, `legacy/register`)
  - `configure.in` / `Makefile.am` (Autotools legacy configs), do not use

## 2. Build, Lint, & Test Commands

### Build
**Use  CMake**
This method is preferred for modern environments.
1. Navigate to the specific project directory (e.g., `cd legacy/register`).
2. Create a separate build directory to keep source clean:
   ```bash
   mkdir build && cd build
   ```
3. Configure the project:
   ```bash
   cmake ..
   ```
   *Note: You may need to specify paths for MINC or BICPL if they are in non-standard locations.*
4. Compile:
   ```bash
   make
   ```

**Option 2: Autotools (Legacy)**
Do not use.

### Linting & Formatting
- **Linting:** No automated linter is currently configured.
  - **Recommended:** Use `cppcheck .` or `clang-tidy` to analyze changes.
  - **Rule:** Fix only errors in the files you touch. Avoid "cleaning up" legacy code unless it fixes a bug.
- **Formatting:** No `.clang-format` exists.
  - **Rule:** strictly follow the **existing coding style** of the file you are editing.
  - **Do NOT** reformat entire files. This obscures the git history.

### Testing
- **Test Runner:** CTest (integrated with CMake).
- **Run All Tests:**
  ```bash
  cd build
  ctest --output-on-failure
  ```
- **Run Specific Tests:**
  ```bash
  ctest -R test_read  # Runs tests matching regex "test_read"
  ```
- **Manual Testing:**
  - Build the test executables (e.g., in `legacy/bicgl/Testing`).
  - Run them directly: `./test_read`

## 3. Code Style & Conventions

### Imports/Includes
- **Order:**
  1. System Headers: `<stdio.h>`, `<stdlib.h>`, `<string.h>`
  2. Library Headers: `<GL/gl.h>`, `<minc/minc.h>`, `<bicpl/globals.h>`
  3. Local Headers: `"graphics.h"`, `"GS_graphics.h"`
- **Syntax:** Use `<>` for external/system libraries and `""` for project-local headers.

### Formatting
- **Indentation:** **4 spaces**. Do not use tabs.
- **Braces:** **Allman style** (braces always on a new line).
  ```c
  // Correct
  if (condition)
  {
      statement();
  }
  
  // Incorrect
  if (condition) {
      statement();
  }
  ```
- **Loops:**
  ```c
  for (i = 0; i < count; ++i)
  {
      // ...
  }
  ```
- **Switch Statements:**
  ```c
  switch (variable)
  {
  case CONSTANT_ONE:
      do_something();
      break;
  default:
      break;
  }
  ```

### Naming Conventions
- **Variables:** `snake_case` (e.g., `volume_filenames`, `tag_filename`).
- **Functions:** `snake_case` (e.g., `initialize_global_colours`, `print_usage`).
  - Public API often uses prefixes: `GS_set_point`, `VIO_Status`.
- **Types/Structs:** `PascalCase` or `Capitalized_Snake_Case` (e.g., `VIO_Point`, `UI_struct`).
- **Macros/Constants:** `UPPER_SNAKE_CASE` (e.g., `N_VOLUMES`, `MAX_RETRY_COUNT`, `DEFAULT_CHUNK_SIZE`).
- **File Names:** `snake_case.c` or `snake_case.h`.

### Error Handling
- **Return Codes:** low level minc Functions often return status codes (e.g., `VIO_Status` which can be `VIO_OK` or `VIO_ERROR`).
  ```c
  if (func() != VIO_OK)
  {
      return VIO_ERROR;
  }
  ```
- **Logging:**
  - Use `print_error(message)` or `fprintf(stderr, ...)` for errors.
  - Do not use `printf` for errors.
- **Memory Management:**
  - Be extremely careful with `malloc` and `free`.
  - Ensure every `malloc` has a corresponding `free` in the appropriate lifecycle method.
  - Use project-specific macros like `FREE()` if available (check `bicpl/globals.h`).

## 4. Workflow Rules for Agents

1. **Analysis First:**
   - Always run `ls -F` and `grep` to locate relevant files.
   - Read the `CMakeLists.txt` to understand dependencies for the file you are editing.
2. **Contextual Awareness:**
   - Read the entire function you are modifying to understand the logic flow.
   - Check `register_UI.globals` or `config.h` if modifying configuration logic.
3. **Atomic & Safe Changes:**
   - do not touch any code in legacy directory, use it only as a reference
   - new code should use C++17 (the CMake standard is set to 17), with modern features available in that standard
4. **Verification:**
   - **Compile:** Always attempt to compile after changes: `make`.
   - **Test:** If modifying logic, write a small test case or run existing tests.
5. **Documentation:**
   - Update Doxygen comments (`/** ... */`) if you change function signatures.
   - Add inline comments (`/* ... */`) for obscure logic.

## 5. Cursor/Copilot Specific Rules
*(No specific rules found in repository)*

- **General:** Be concise and efficient.
- **Tech-Specific:**
  - Respect the legacy C nature of the project.
  - Prefer `snake_case` over `camelCase`.
  - Do not assume modern C++ standard library availability (e.g., `std::vector`) unless you see it being used.

## 6. Modern Rewrite (new_register)

This is a feature-complete rewrite of the `register` application using C++17,
Vulkan (primary) / OpenGL 2.1 (fallback), ImGui (Docking), GLFW, and nlohmann/json.

> **See also:** [`research.md`](research.md) for a comprehensive codebase review,
> architecture diagram, module analysis, test coverage, and risk assessment.
> [`mincpik_performance_review.md`](mincpik_performance_review.md) for a ranked
> list of CPU bottlenecks in `new_mincpik` with proposed fixes.

**Dependencies:**
- `minc2-simple` (FetchContent from GitHub, develop branch)
- `ImGui` (fetched via CMake)
- `GLFW` (system)
- `Vulkan` (system, optional — OpenGL2 fallback available)
- `nlohmann/json` (v3.11.3, FetchContent)
- `Eigen` (FetchContent, for transform math)

**Structure:**
- `new_register/src/`: Source files (~15 `.cpp` files + `qc/` subdirectory)
- `new_register/include/`: Headers mirroring `src/`
- `new_register/tests/`: 16 CTest suites

**Build Instructions:**
```bash
cd new_register/build
cmake ..
make -j$(nproc)
ctest --output-on-failure
```

**Implemented Features:**
- MINC2 volume loading via `minc2-simple`, multi-volume side-by-side display
- Three orthogonal slice views per volume (sagittal, coronal, axial)
- Crosshair navigation with optional cursor sync across volumes
- 21 colour maps, per-volume display range, zoom/pan per view
- Overlay compositing (volume 2 alpha-blended over volume 1)
- Tag point placement, editing, and `.tag` file I/O
- Registration transforms: LSQ6/7/9/10/12 and TPS (Eigen SVD)
- QC (Quality Control) batch review mode — see below
- Screenshot capture (`P` key → `screenshot000001.png`, auto-incrementing)
- JSON config persistence (load/save `config.json`)
- Hotkey reference panel in the UI

**Coding Standards (New Project):**
- C++17 (`CMAKE_CXX_STANDARD 17`); `std::vector`, `std::string`, smart pointers allowed.
- `PascalCase` for classes, `camelCase` for methods/variables.
- `std::cerr` for error output; never `printf` to stderr.
- Namespace `QC::` for all code in `src/qc/`.

**Hotkey Reference Panel:**
- `Interface::renderHotkeyPanel()` in `Interface.cpp` shows all current keyboard
  shortcuts and mouse interactions in the running UI.
- **When adding, removing, or changing any hotkey or mouse interaction, update
  `renderHotkeyPanel()` to keep the panel accurate.**

### Graphics Backend Design

The backend is selected at runtime with a fallback chain:
**Vulkan → OpenGL2 → OpenGL2-EGL**

- `BackendFactory.cpp` drives selection; `--backend` CLI flag overrides.
- `GraphicsBackend` (abstract base) defines the interface: `createTexture`,
  `updateTexture`, `beginFrame`, `endFrame`, `captureScreenshot`, etc.
- `VulkanBackend.cpp` — primary; uses ImGui's Vulkan backend + custom helpers.
- `OpenGL2Backend.cpp` — fallback for SSH / software renderers (no compute,
  no Vulkan ICD). Uses `glTexSubImage2D`, which is async from the application.
- `VulkanHelpers.cpp` — persistent staging buffer + dedicated upload command pool.
  Texture uploads use a `VkFence` (not `vkQueueWaitIdle`) so the GPU processes
  the previous upload in parallel with CPU pixel generation.

### Window Resizing Behavior

Resizing is handled in two coordinated layers:

1. **Swapchain rebuild** (`WindowManager.cpp`):
   - GLFW calls `framebufferCallback` on any framebuffer size change.
   - `WindowManager::onFramebufferResize()` sets `swapchainRebuildPending_ = true`.
   - The main render loop checks `needsSwapchainRebuild()` and calls
     `backend.rebuildSwapchain(w, h)` before the next frame.
   - The Vulkan swapchain is rebuilt with the new dimensions; OpenGL2 backend
     updates its viewport instead.

2. **Dock layout rebuild** (`Interface.cpp`, `render()`):
   - Each frame, `Interface::render()` compares the current ImGui viewport size
     against `lastViewportSize_`. If they differ by more than 0.5 px, the entire
     ImGui dock layout is rebuilt from scratch using `DockBuilder`.
   - **All splits are fractional**, so every panel resizes proportionally with
     the window — no panel keeps a fixed pixel width after a resize.
   - The Tools (left) panel width fraction adapts to the number of content columns:
     - 1 column → 30 %, 2 → 20 %, 3 → 16 %, 4+ → 13 %
     - QC mode adds +2 % for the embedded QC row list.
   - Content columns (volumes + optional Overlay) are split equally in the
     remaining space using a recursive left-split loop.
   - The dock rebuild is also triggered when volumes are added or removed
     (`state_.layoutInitialized_` is cleared on volume list changes).

## 7. C++23 Modernization (new_register)

The codebase currently targets C++17 (`CMAKE_CXX_STANDARD 17`). A future upgrade to C++23 is planned. Phases 1-2 are complete (unique_ptr, std::array). Remaining phases (blocked on C++23 upgrade):

### Phase 3: Range-Based For and C++23 Ranges
- ColourMap.cpp: Replace manual for loops with `std::views::iota` + lambda
- Volume.cpp: Use `std::ranges::minmax_element` for min/max calculation
- Volume.cpp: Use range-based for for dimension loops

### Phase 4: constexpr and consteval
- ColourMap.cpp: Replace custom `countOf` template with `std::size()`
- Consider making `packRGBA` constexpr

### Phase 5: auto Type and Structured Bindings
- AppConfig.cpp: Use structured bindings in merge loop
- TagWrapper.cpp: Use range-based for for point/label copy loops
- main.cpp: Use direct assignment for config serialization

### Phase 6: Replace Magic Numbers
- Volume.cpp: Replace hardcoded `256` with `dimensions.x * dimensions.y * dimensions.z`
- main.cpp: Replace `sizeof(quickMaps) / sizeof(quickMaps[0])` with `std::size(quickMaps)`

### Phase 7: Use std::format
- Replace `printf`/`fprintf`/`std::cout` with `std::print` where applicable

### Build & Test
```bash
cd new_register/build
cmake .. && make
ctest --output-on-failure
```

## 8. QC Modes — Two Separate Tools

There are two distinct QC workflows, each producing a separate binary:

---

### 8a. `new_register --qc` (Integrated MINC QC mode)

Full MINC2 volume QC, embedded in the `new_register` application.
Implemented in `new_register/src/QCState.cpp` and `Interface.cpp`.

**Run:**
```bash
./new_register --qc input.csv --qc-output results.csv [--config qc_config.json]
```

**CSV format:**
- Input: first column `ID`, remaining columns are MINC2 volume file paths
  (one column per volume to display side by side).
- Output: for each input column, two output columns: `<name>_verdict` and
  `<name>_comment`. Verdicts: `PASS`, `FAIL`, or empty (unreviewed).

**Design choices:**
- QC verdict state lives in `QCState` (not `AppState`); `Interface` polls it.
- Volumes for a row are loaded by `Prefetcher` on the main thread (libminc/HDF5
  is not thread-safe). The previous/next row is prefetched speculatively.
- Tag creation and the tag list are completely disabled in QC mode.
- "Save Local" button is hidden; output CSV is auto-saved on every verdict change.
- Clean mode (`C`) hides control panels but **keeps verdict panels visible**.
- QC list panel (left, embedded in Tools) shows all rows with colour-coded status:
  green = all pass, red = any fail, gray = unreviewed.
- Navigate with `[` / `]` keys or by clicking rows in the QC list.
- On launch, if the output CSV already exists, previous verdicts are loaded and
  the viewer jumps to the first unreviewed row.

---

### 8b. `new_qc` (Standalone image QC viewer)

A lightweight standalone viewer for reviewing arbitrary images (PNG, JPG, etc.)
from a CSV manifest. Separate binary built from `new_register/src/qc/`.

**Structure:**
- `new_register/src/qc/main.cpp` — CLI entry point
- `new_register/src/qc/QCApp.h/.cpp` — GLFW+ImGui+Vulkan/OpenGL2 GUI, image
  loading (stb_image), keyboard input
- `new_register/src/qc/CSVHandler.h/.cpp` — CSV I/O, `QCRecord` struct, RFC 4180
- `new_register/src/qc/VulkanBackend.*` — Vulkan primary backend (own copy)
- `new_register/src/qc/OpenGL2Backend.*` — OpenGL2 fallback (own copy)
- `new_register/tests/csv_test.cpp` — 42 unit tests for `CSVHandler`

**Build (produces `new_qc` alongside `new_register`):**
```bash
cd new_register/build && cmake .. && make
```

**Run:**
```bash
./new_qc input.csv output.csv [--scale <factor>]
```

**CSV Formats:**
- Input: `id,visit,picture` (3 columns — id, visit, image file path)
- Output: `id,visit,picture,QC,notes` (5 columns)

**Keyboard Shortcuts:**
- `P` — Mark Pass (auto-advances)
- `F` — Mark Fail (auto-advances)
- `←` / `Page Up` — Previous case
- `→` / `Page Down` — Next case
- `Ctrl+S` — Manual save
- `Esc` — Exit

**Coding standards:**
- C++17, namespace `QC::` for all classes in `src/qc/`
- `PascalCase` classes, `camelCase` methods/variables
- `std::cerr` for error output
