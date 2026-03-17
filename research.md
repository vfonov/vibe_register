# research.md — Comprehensive Codebase Review

> **Last updated:** 2026-03-16 (merged new_qc into new_register)
> **Scope:** Full source review of `new_register` (merged codebase: 24 `.cpp`, 19 headers in src/, 16 test suites).

---

# Part A — new_register

> **Original review date:** 2026-03-04

---

## 1. Project Summary

`new_register` is a modern C++17 rewrite of the legacy BIC `register` medical image
viewer/registration tool. It provides multi-volume MINC2 viewing with 3-plane slice
views, alpha-blended overlay compositing, tag-point registration (6 transform types),
QC batch review mode, and dual GPU backends (Vulkan + OpenGL 2).

**Codebase size:** ~13,900 lines across 24 `.cpp` + 19 `.h` files in `src/`, plus ~3,700 lines
of tests across 16 test suites.

**Note:** `new_qc` was merged into `new_register/src/qc/` on 2026-03-16 (commit a75fb1a).

**Build system:** CMake with FetchContent for ImGui, nlohmann/json, GLM, cxxopts, and
ExternalProject for Eigen, libminc, minc2-simple.

---

## 2. Architecture

```
    ┌──────────────┐
    │   main.cpp    │  CLI parsing, window creation, render loop
    │  (800 lines)  │
    └──────┬────────┘
           │
    ┌──────▼────────┐    ┌───────────────┐
    │   Interface    │───►│  ViewManager   │  Texture generation, sync, overlay
    │ (2273 lines)   │    │  (734 lines)   │
    └──────┬────────┘    └──────┬────────┘
           │                    │
    ┌──────▼────────┐    ┌──────▼─────────────┐
    │   AppState     │    │ GraphicsBackend     │  Abstract GPU interface
    │  (435 lines)   │    │  (157 lines hdr)    │
    └──┬───┬───┬────┘    └──┬───────────┬─────┘
       │   │   │            │           │
       │   │   │    ┌───────▼───┐  ┌────▼──────────┐
       │   │   │    │  Vulkan    │  │  OpenGL 2     │
       │   │   │    │  Backend   │  │  Backend      │
       │   │   │    │ (1056 ln)  │  │  (316 lines)  │
       │   │   │    └────────────┘  └───────────────┘
       │   │   │
  ┌────▼┐ ┌▼─────┐ ┌▼───────────┐
  │Volume│ │ Tags  │ │ Transform  │
  │468 ln│ │231 ln │ │ 1136 lines │
  └──────┘ └───────┘ └────────────┘
```

### Layer Separation

| Layer | Files | Responsibility |
|-------|-------|----------------|
| **Data** | `Volume`, `TagWrapper`, `AppState`, `AppConfig`, `QCState` | Volume I/O, tag management, state, config persistence |
| **Logic** | `ViewManager`, `Transform`, `ColourMap`, `Prefetcher` | Slice rendering, transforms, colour mapping, prefetch |
| **Presentation** | `Interface` | ImGui UI, mouse/keyboard events |
| **GPU** | `GraphicsBackend`, `VulkanBackend`, `OpenGL2Backend`, `VulkanHelpers` | Texture management, frame lifecycle |

### Key Design Decisions

- **Abstract backend**: `GraphicsBackend` provides a unified interface; `BackendFactory` selects at runtime with automatic fallback (Vulkan -> OpenGL2 -> OpenGL2-EGL).
- **Synchronous prefetching**: Despite `Prefetcher` having thread-safety primitives, actual loading is main-thread-only because libminc/HDF5 are not thread-safe.
- **LRU volume cache**: `VolumeCache` (default 8 entries) avoids re-reading MINC files during QC row switches.
- **CPU compositing**: Overlay blending and colour mapping are done in software (not GPU shaders).

---

## 3. Module-by-Module Analysis

### 3.1 main.cpp (966 lines)

Entry point, CLI parsing (via `cxxopts`), GLFW window creation, main render loop.

- Backend selection with multi-level fallback (Vulkan -> OpenGL2 -> OpenGL2-EGL)
- DPI-aware window sizing with monitor workarea clamping
- QC mode initialization with CSV input/output and prefetching
- Main loop: poll events -> prefetch one volume -> check swapchain -> ImGui frame -> render -> present

**Strengths:** Robust backend fallback; clean QC initialization.
**Concerns:** `main()` is ~800 lines and could benefit from extraction into setup helpers.

### 3.2 AppState.cpp (435 lines)

Central application state, volume lifecycle, tag management, transform dispatch.

- **VolumeCache**: LRU cache (std::list + std::unordered_map) with mutex, default capacity 8
- Volume loading delegates to `Volume::load()` with name extraction from path
- Tag auto-discovery by replacing `.mnc` extension with `.tag`
- Combined two-volume tag I/O via `TagWrapper`
- Config application maps JSON settings to view states (by index, not by path)

**Strengths:** Clean volume lifecycle; LRU cache useful for QC.
**Concerns:** `applyConfig()` matches volumes by index — renaming/reordering breaks the mapping. `loadCombinedTags()` only distributes to volumes 0 and 1.

### 3.3 Volume.cpp (468 lines)

MINC2 volume representation: loading, coordinate transforms, label support.

- RAII `Minc2Handle` wraps `minc2_file_handle` (allocate on construction, close+free on destruction)
- `load()` reads dimensions, builds voxel-to-world and world-to-voxel 4x4 matrices from direction cosines
- Bounds-checked `get()` returns 0.0f for out-of-bounds
- Label description file parser for FreeSurfer-style LUT

**Strengths:** Correct matrix construction; RAII handle; clean label LUT parsing.
**Concerns:** Reads entire volume into memory as float (no lazy/mmap). Copy constructor duplicates full data vector.

### 3.4 ColourMap.cpp (395 lines)

21 colour maps via piecewise-linear interpolation of control points.

- Static control-point arrays define all maps
- Lazy static initialization (built once, thread-safe)
- Name-to-enum and enum-to-name lookups

**Strengths:** Clean data-driven design.
**Note:** Uses custom `countOf()` template — could be `std::size()`.

### 3.5 Interface.cpp (2720 lines) — Largest file

Full ImGui UI: dock layout, volume columns, overlay panel, tag list, QC verdicts, file dialogs, hotkey panel.

- `renderSliceView()` (335 lines): texture display, zoom/pan UV mapping, crosshairs, tag drawing, mouse interaction
- `renderOverlayView()` (246 lines): similar to slice view but for overlay
- `switchQCRow()` (84 lines): saves display settings, loads new volumes, restores, prefetches
- `renderToolsPanel()` (226 lines): sync checkboxes, QC controls, config I/O
- `renderVolumeColumn()` (366 lines): per-volume controls and 3 slice views
- `renderHotkeyPanel()` (new): displays keyboard shortcuts reference

**Strengths:** Comprehensive feature coverage; clean data/presentation separation.
**Concerns:** File is oversized (candidate for splitting). Significant code duplication between `renderSliceView()` and `renderOverlayView()`.

### 3.6 ViewManager.cpp (734 lines)

Slice texture generation, overlay compositing, view synchronization.

- `updateSliceTexture()`: generates RGBA pixel buffers from volume data with colour mapping
- `updateOverlayTexture()`: composites volumes with alpha blending; scanline-optimized for linear transforms, per-pixel Newton-Raphson for TPS
- Cursor/zoom/pan synchronization across volumes

**Strengths:** Scanline-optimized compositing for linear transforms; pixel buffer reuse.
**Concerns:** TPS overlay path is O(n_tags * pixels). All compositing is CPU-bound.

### 3.7 Transform.cpp (1136 lines)

Tag-point registration: LSQ6/7/9/10/12 + TPS.

- SVD Procrustes for initial guess (LSQ6/7); polar decomposition for LSQ9/10
- Eigen Levenberg-Marquardt for iterative refinement
- Direct QR decomposition for LSQ12
- Full thin-plate spline with kernel assembly and linear solve
- Newton-Raphson TPS inversion for `inverseTransformPoint()`
- MNI `.xfm` file I/O (linear via minc2-simple, TPS via manual text output)

**Strengths:** Mathematically rigorous; well-structured parameterization (`R * S` for correct convergence).
**Note:** LSQ10 has no dedicated test.

### 3.8 VulkanBackend.cpp (1096 lines) + VulkanHelpers.cpp (406 lines)

Full Vulkan backend: device selection, swapchain, textures, screenshots.

- Two-pass GPU selection (hardware first, then software/CPU)
- SIGSEGV guard via `sigsetjmp`/`siglongjmp` for lavapipe crashes
- Persistent staging buffer (grows to power-of-2, never shrinks)
- Screenshot via staging buffer with BGRA->RGBA swizzle

**Strengths:** Robust device selection with fallback; persistent staging avoids allocation churn.
**Concerns:** Static global state in `VulkanHelpers` (not re-entrant). `vkQueueWaitIdle()` after every texture upload serializes GPU work. SIGSEGV catching is fragile.

### 3.9 OpenGL2Backend.cpp (316 lines)

Legacy OpenGL 2 fixed-function backend for SSH/X11/software renderers.

- `GL_CHECK()` error macro around all GL calls
- Simple texture management via `glGenTextures`/`glTexImage2D`

**Strengths:** Simple, works everywhere.
**Concerns:** No error recovery — `GL_CHECK` logs but does not abort.

### 3.10 QCState.cpp (364 lines)

QC batch review: CSV I/O, verdict tracking, rated/unrated counts.

- RFC 4180 CSV parsing with proper quote handling
- Input/output CSV matching by ID column
- Per-column verdict/comment tracking

**Strengths:** Well-structured; 88% test coverage.

### 3.11 TagWrapper.cpp (231 lines)

RAII C++ wrapper around minc2-simple C tag API.

- Bidirectional conversion: C tag structs <-> C++ `glm::dvec3` vectors
- Two-volume tag file support
- Move and copy semantics (copy only copies high-level data)

**Strengths:** Clean RAII; proper cleanup in destructor.

### 3.12 AppConfig.cpp (150 lines)

JSON config serialization via nlohmann/json.

- `to_json`/`from_json` overloads for all config structs
- Creates parent directories on save
- 100% test coverage

### 3.13 Prefetcher.cpp (59 lines)

Main-thread synchronous volume prefetching for QC mode.

- Loads one volume per frame to avoid blocking UI
- Skips already-cached volumes
- Intentionally synchronous (libminc/HDF5 not thread-safe)

### 3.14 BackendFactory.cpp (76 lines)

Factory pattern for graphics backends with compile-time conditionals and string aliases.

### 3.15 SliceRenderer.cpp (729 lines) — NEW

Slice rendering abstraction layer for multi-volume visualization.

- Manages slice orientation (sagittal, coronal, axial) and coordinate transforms
- Handles texture coordinate mapping for zoom/pan operations
- Crosshair rendering and world-coordinate synchronization

**Strengths:** Clean abstraction for slice rendering logic.
**Concerns:** Tightly coupled with ViewManager.

### 3.16 QC Module (src/qc/) — MERGED

The `new_qc` tool was merged into `new_register/src/qc/` on 2026-03-16. See **Part B** for full analysis.

**Files added:**
- `qc/main.cpp` (153 lines): CLI entry point for standalone QC tool
- `qc/QCApp.cpp` (537 lines) + `qc/QCApp.h` (70 lines): GLFW+ImGui+OpenGL GUI application
- `qc/CSVHandler.cpp` (245 lines) + `qc/CSVHandler.h` (58 lines): CSV I/O with RFC 4180 compliance
- `qc/VulkanBackend.cpp` (945 lines) + `qc/VulkanBackend.h` (82 lines): Vulkan GPU backend for QC
- `qc/VulkanHelpers.cpp` (378 lines) + `qc/VulkanHelpers.h` (33 lines): Vulkan helper functions
- `qc/OpenGL2Backend.cpp` (318 lines) + `qc/OpenGL2Backend.h` (57 lines): OpenGL2 fallback backend
- `qc/Backend.h` (136 lines): Abstract backend interface
- `qc/BackendFactory.cpp` (62 lines): Backend selection factory

**Key differences from new_register:**
- JPEG/PNG image datasets instead of MINC2 volumes
- Keyboard-driven Pass/Fail workflow with notes field
- CSV-based persistence (input: 3 columns, output: 5 columns)
- HiDPI support with `--scale` override option
- No MINC/HDF5 dependencies (BUILD_QC_ONLY mode)

---

## 4. Dependencies

| Dependency | Source | Version | Purpose |
|------------|--------|---------|---------|
| **libminc** | FetchContent (system fallback) | develop | MINC2 I/O |
| **minc2-simple** | FetchContent | develop | Simplified MINC2 API, tag I/O |
| **ImGui** | FetchContent | docking branch | UI framework |
| **nlohmann/json** | FetchContent | v3.11.3 | JSON config persistence |
| **GLM** | FetchContent | v1.0.1 | Math (vectors, matrices) |
| **Eigen** | FetchContent | v3.4.0 | Levenberg-Marquardt optimizer |
| **cxxopts** | FetchContent | v3.2.1 | CLI argument parsing |
| **GLFW** | System | — | Window management |
| **Vulkan** | System (optional) | — | GPU backend |
| **OpenGL** | System (optional) | — | Legacy GPU backend |
| **HDF5** | System | — | Required by MINC2 |
| **stb_image_write** | Vendored header | v1.16 | PNG screenshot output |

---

## 5. Testing

### Test Suite Summary

| Test Name | Source | What it tests |
|-----------|--------|---------------|
| `ReadRealData` | test_real_data.cpp | Volume loading, dimensions, data statistics |
| `CoordinateSyncTest` | test_coordinate_sync.cpp | Cross-volume voxel sync via world coords |
| `CenterOfMassTest` | test_center_of_mass.cpp | Center of mass via transformVoxelToWorld |
| `ThickSlicesCOMTest` | test_thick_slices_com.cpp | COM for non-isotropic volumes |
| `WorldToVoxelTest` | test_world_to_voxel.cpp | Voxel <-> world round-trips |
| `MatrixDebugTest` | test_matrix_debug.cpp | Volume metadata and coordinate transforms |
| `TagLoadTest` | test_tag_load.cpp | TagWrapper load/save for 1 and 2 volume files |
| `VolumeInfoTest` | test_volume_info.cpp | Raw MINC2 API metadata validation |
| `MincDimsTest` | test_minc_dims.cpp | Raw MINC2 dimension metadata |
| `ColourMapTests` | test_colour_map.cpp | All 21 LUT endpoints, name lookup |
| `LabelRoundingTest` | test_label_rounding.cpp | Label value rounding correctness |
| `QCCsvTest` | test_qc_csv.cpp | CSV parsing, quoting, round-trip, edge cases |
| `AppConfigTest` | test_app_config.cpp | JSON config round-trip, defaults, optional fields |
| `TransformTest` | test_transform.cpp | All transform types, XFM I/O |
| `MincpikTest` | test_mincpik.cpp | Mosaic image generation (NEW) |
| `CsvTest` | csv_test.cpp | CSVHandler unit tests (42 tests, NEW) |

### Coverage by Module

| Module | Public Functions | Tested | Coverage |
|--------|-----------------|--------|----------|
| AppConfig | 10 | 10 | **100%** |
| QCState | 8 | 7 | **88%** |
| Transform | 6 | 5 | **83%** |
| ColourMap | 5 | 3 | **60%** |
| TagWrapper | 18 | 10 | **56%** |
| Volume | 18 | 8 | **44%** |
| AppState | 25 | 0 | **0%** |

**Approximate overall coverage of unit-testable functions: ~35% (improved with CSVHandler tests).**

### Coverage Gaps (Priority Order)

**HIGH — AppState (0%):**
- `VolumeCache` get/put/eviction
- `getTagPairs()`, `applyConfig()`, `initializeViewStates()`
- `saveTags()` routing, `loadCombinedTags()`, `saveCombinedTags()`

**MEDIUM — TagWrapper mutations:**
- `removeTag()`, `updateTag()`, `clear()`

**MEDIUM — Volume utilities:**
- `get()` bounds check, `worldExtent()`, `slicePixelAspect()`, `generate_test_data()`

**MEDIUM — QC Module (QCApp untested):**
- `loadImage()`, `markAsPass/Fail()`, navigation logic
- GLFW window initialization, HiDPI scaling

**LOW:**
- Copy/move semantics (Volume, TagWrapper)
- Prefetcher integration test
- BackendFactory name parsing
- SliceRenderer unit tests

---

## 6. Discrepancies Between Documentation and Code

| Item | AGENTS.md says | Actual code |
|------|----------------|-------------|
| **C++ standard** | C++23 | `CMAKE_CXX_STANDARD 17` in CMakeLists.txt |
| **JSON library** | Glaze | nlohmann/json v3.11.3 |
| **minc2-simple source** | `legacy/minc2-simple` | FetchContent from GitHub |
| **Source file list** | `main.cpp, Volume.cpp, VulkanHelpers.cpp` | 15 source files |
| **Prefetcher threading** | PLAN.md says background `std::thread` | Actually synchronous main-thread loading |
| **Error output** | `std::cerr` or modern | Mix of `fprintf(stderr)`, `std::cerr` |

---

## 7. Potential Issues and Risks

### 7.1 Segfault Risks (HIGH)

1. **VulkanHelpers static globals** (`VulkanHelpers.cpp`): Device, queue, and pool are file-scope statics. Out-of-order shutdown or missing `Shutdown()` call leaves dangling state.
2. **Volume copy** (`Volume.cpp`): Copying a volume with a large data vector (potentially hundreds of MB) could cause allocation failure with no `try`/`catch`.
3. **ViewManager index bounds** (`ViewManager.cpp`): `updateSliceTexture(volumeIndex, viewIndex)` does not validate that `volumeIndex` is within `volumes_` range. Out-of-range access would segfault.
4. **SIGSEGV guard** (`VulkanBackend.cpp`): `sigsetjmp`/`siglongjmp` to catch Vulkan driver crashes is inherently fragile and could mask real bugs.
5. **Tag index bounds** (`TagWrapper.cpp`): `removeTag()` and `updateTag()` should validate index before accessing vectors.

### 7.2 Thread Safety (MEDIUM)

- `VolumeCache` has mutex protection, but `Prefetcher` actually runs on the main thread — the mutex is harmless overhead.
- If Prefetcher were made truly threaded in the future, `Volume::load()` would not be safe (libminc/HDF5 use global state).
- No synchronization on `AppState` members accessed from both UI and prefetch paths.

### 7.3 Performance (MEDIUM)

- **CPU-bound overlay compositing**: All blending and colour mapping happens on the CPU. GPU shaders would be significantly faster.
- **TPS overlay**: Per-pixel Newton-Raphson inversion is O(n_tags * pixels) — potentially very slow with many tags or large views.
- **Vulkan texture upload**: `vkQueueWaitIdle()` after every texture upload creates a full pipeline stall. A fence or timeline semaphore would allow overlap.
- **Full volume in memory**: `Volume::load()` reads the entire 3D volume as float. For very large volumes (e.g., 512^3), this is ~500MB per volume.

### 7.4 Code Quality (LOW)

- `Interface.cpp` (2273 lines) is the largest file and a candidate for splitting into tag UI, QC UI, file dialogs, and slice rendering.
- `renderSliceView()` and `renderOverlayView()` share ~60% of their logic — could be unified with a mode parameter.
- `main()` at ~600 lines could be decomposed into initialization helpers.
- C++23 modernization phases 3-7 (from AGENTS.md section 7) remain undone.

---

## 8. Completed Features

A feature-rich, functional application that covers the core functionality of the legacy `register` tool:

- Multi-volume MINC2 viewer with 3-plane slice views and crosshairs
- Dual GPU backends (Vulkan + OpenGL 2) with automatic fallback and EGL support
- 21 colour maps with per-volume selection and under/over colour modes
- Alpha-blended overlay with world-coordinate resampling
- Full tag-point registration (LSQ6/7/9/10/12 + TPS) with LM optimization
- Transform visualization in overlay (scanline-optimized linear, per-pixel TPS)
- QC batch review mode with CSV I/O, per-column verdicts, and prefetching
- Label volume support with FreeSurfer LUT parsing
- JSON config persistence with file dialogs
- Screenshot (PNG) for both backends
- 14 passing test suites

---

## 9. Not Yet Implemented

From PLAN.md — features remaining from the legacy tool:

- **Resampling**: Resample volume 2 into volume 1's space using computed transform
- **Slice filtering**: Per-view filter type (nearest, linear, box, triangle, gaussian) with configurable FWHM
- **Interpolation**: Per-volume interpolation type selection
- **Additional volume support**: Up to 8 volumes, NIfTI-1 format, RGB/vector volumes, 4D volumes
- **UI features**: Tag label editing in table, per-volume load buttons, quit confirmation dialog
- **Metal backend**: macOS support (stub exists in CMakeLists.txt)
- **WebAssembly build**: Plan exists in PLAN.md but not started

---

## 10. Recommendations

### Immediate (Stability)
1. Add bounds checking in `ViewManager` for volume/view indices before array access.
2. Add null/bounds checks in `TagWrapper::removeTag()` and `updateTag()`.
3. Review `VulkanHelpers` lifecycle — ensure `Shutdown()` is always called before process exit.

### Short-term (Quality)
4. Split `Interface.cpp` into 3-4 files (tag UI, QC UI, slice rendering, file dialogs).
5. Unify `renderSliceView()` and `renderOverlayView()` to eliminate duplication.
6. Add `AppState` unit tests — it is 0% covered and contains critical logic.
7. Add LSQ10 transform test.

### Medium-term (Modernization)
8. Update `CMAKE_CXX_STANDARD` from 17 to 23 and execute phases 3-7.
9. Replace `vkQueueWaitIdle()` with fence-based synchronization.
10. Consider GPU-based overlay compositing for large volumes.

### Long-term (Features)
11. Implement resampling and slice filtering.
12. Add NIfTI-1 support for broader compatibility.
13. Metal backend for macOS.

---

# Part B — new_qc (Merged into new_register)

> **Review date:** 2026-03-16 (merged)
> **Scope:** Full source review of QC module in `new_register/src/qc/` (8 `.cpp`, 6 headers, 42-test suite).

---

## B.1 Project Summary

`new_qc` is a lightweight Quality Control (QC) tool for reviewing medical imaging
datasets represented as JPEG/PNG images. It provides a fast, keyboard-driven Pass/Fail verdict
workflow with notes, CSV-based persistence, and resume support. Designed for large batches
(100s–1000s of cases) where speed and simplicity matter more than volumetric detail.

**Status:** Merged into `new_register/src/qc/` on 2026-03-16 (commit a75fb1a).

**Codebase size:** ~3,100 lines across 8 `.cpp` + 6 `.h` files in `src/qc/`, plus 42 unit tests.

**Version:** 1.0.0

**Build system:** Integrated into `new_register` CMake with `BUILD_QC_ONLY` option for standalone builds.

---

## B.2 Architecture

```
    ┌─────────────────────────────┐
    │         main.cpp            │  CLI parsing, creates QCApp, runs it
    └──────────────┬──────────────┘
                   │
         ┌─────────▼──────────┐
         │      QCApp          │  GLFW window, ImGui UI, OpenGL textures,
         │  (QCApp.h/.cpp)     │  image loading (stb_image), keyboard input
         └─────────┬──────────┘
                   │
         ┌─────────▼──────────┐
         │    CSVHandler       │  CSV I/O: load input/output, save progress
         │ (CSVHandler.h/.cpp) │
         └────────────────────┘
```

### Layer Separation (Standalone Mode)

| Layer | Files | Responsibility |
|-------|-------|----------------|
| **Entry** | `qc/main.cpp` | CLI args, creates & runs QCApp |
| **Application** | `qc/QCApp.h/.cpp` | GUI, image loading, navigation, QC decisions |
| **Persistence** | `qc/CSVHandler.h/.cpp` | CSV parsing, field escaping, load/save |
| **GPU Backend** | `qc/VulkanBackend`, `qc/OpenGL2Backend` | Vulkan primary, OpenGL2 fallback |

**Note:** When built with `BUILD_QC_ONLY`, the QC tool is standalone with no MINC dependencies.

---

## B.3 Module-by-Module Analysis (Merged Structure)

### B.3.1 qc/main.cpp (153 lines)

Entry point, CLI argument parsing (manual, no cxxopts), display-guard.

- Arguments: `input_csv output_csv [--scale <factor>] [--help] [--version]`
- Validates that a display is available (checks `DISPLAY`/`WAYLAND_DISPLAY`); suggests `xvfb-run` for servers
- Creates `QC::QCApp`, calls `init()` → `run()` → `shutdown()`

**Concerns:** No library for CLI parsing — manual string matching is error-prone for future flags.

---

### B.3.2 QCApp.h/.cpp (537 lines + 70 lines header)

Main application class. Owns GLFW window, ImGui context, OpenGL/Vulkan textures, image data, and navigation state.

**Key data members:**
- `window_` — GLFW window pointer
- `csvHandler_` — embedded CSVHandler
- `currentImage_` — `ImageData` struct: `pixels` (RGBA bytes), `width`, `height`, `textureId` (GL)
- `currentIndex_` — current record index
- `running_` — main loop control
- `currentScale_`, `imageScale_` — HiDPI content and display scale factors
- `autoSave_` — bool, default `true`

**Core methods:**

| Method | Behaviour |
|--------|-----------|
| `init(inputFile, outputFile, scale)` | Load/resume CSV, init GLFW+OpenGL+ImGui, load first image |
| `run()` | Main loop: poll events → new ImGui frame → `renderUI()` → swap |
| `shutdown()` | Free GL texture, ImGui teardown, GLFW destroy |
| `loadImage(path)` | stb_image load → GL_RGBA texture; handles missing file gracefully |
| `markAsPass()` / `markAsFail()` | Update status, advance to next unrated, auto-save |
| `navigateTo(index)` | Bounds-checked jump + scroll case list |
| `renderUI()` | Two-column ImGui layout (left: controls/list, right: image) |
| `renderImage()` | Aspect-ratio-preserving image display within right panel |
| `handleKeyboard()` | P=Pass, F=Fail, ←/→=prev/next, Ctrl+S=save, Esc=exit |

**GLFW callbacks:** `glfwKeyCallback`, `glfwWindowSizeCallback`, `glfwCloseCallback` — all delegate to `QCApp` methods via the user pointer.

**UI layout:**
- Left panel (~30% width): navigation buttons, subject info, QC buttons (Pass/Fail), notes text field, progress bar, color-coded case list table (green=Pass, red=Fail, gray=unrated)
- Right panel (~70% width): full-panel image with aspect-ratio-preserving letterboxing

**Graphics:** OpenGL 3.3 core profile only (no Vulkan, no backend abstraction). Single texture recycled per navigation.

**Strengths:** Simple, self-contained, fast for large image datasets.
**Concerns:**
- No per-volume config (colour map, zoom) — not relevant for JPEG/PNG but limits MINC2 use.
- `nlohmann_json` is a listed dependency but currently unused — hints at planned JSON config.
- No prefetching — each navigation incurs a synchronous file read.

---

### B.3.3 CSVHandler.h/.cpp

All CSV file I/O and the `QCRecord` data structure.

**`QCRecord` struct:**
```cpp
struct QCRecord {
    std::string id;
    std::string visit;
    std::string picture_path;
    std::string qc_status;   // "Pass", "Fail", or ""
    std::string notes;
};
```

**Key methods:**

| Method | Description |
|--------|-------------|
| `loadInputCSV(file)` | Loads 3-column input (id, visit, picture); auto-detects header row; initializes status/notes as empty |
| `loadOutputCSV(file)` | Loads 5-column output (id, visit, picture, QC, notes); returns false if file missing (graceful resume) |
| `saveOutputCSV(file)` | Writes all records with proper RFC 4180 escaping |
| `parseCSVLine(line)` | Quote-aware field splitting |
| `escapeCSVField(field)` | Wraps fields containing `,`, `"`, or `\n` in quotes; doubles internal quotes |
| `trim(s)` | Strip leading/trailing whitespace |

**CSV formats:**
- Input:  `id,visit,picture`
- Output: `id,visit,picture,QC,notes`

**Strengths:** Correct RFC 4180 handling; header auto-detection; 42-test coverage.
**Concerns:** `id` column is assumed to be column 0 — no configurable key column. Header detection uses a heuristic (`"id"`, `"subject"`, `"patient"` literals).

### B.3.4 Backend Modules (src/qc/)

The QC module includes backend abstraction for GPU rendering:

- **qc/VulkanBackend.cpp** (945 lines): Primary Vulkan backend for image display
- **qc/VulkanHelpers.cpp** (378 lines): Vulkan helper functions (device selection, swapchain, texture upload)
- **qc/OpenGL2Backend.cpp** (318 lines): OpenGL2 fallback backend for software renderers
- **qc/Backend.h** (136 lines): Abstract backend interface
- **qc/BackendFactory.cpp** (62 lines): Backend selection with automatic fallback

**Strengths:** Backend abstraction allows flexibility; Vulkan primary with OpenGL2 fallback.
**Concerns:** VulkanHelpers uses static globals (same as new_register).

---

## B.4 Dependencies

| Dependency | Source | Version | Purpose |
|------------|--------|---------|---------|
| **GLFW3** | System | 3.3+ | Window management, keyboard input |
| **OpenGL** | System | 3.3+ | Rendering backend (fallback) |
| **Vulkan** | System (optional) | — | Rendering backend (primary) |
| **ImGui** | FetchContent | v1.92.0 (docking) | Immediate-mode GUI |
| **stb_image** | FetchContent (header-only) | — | JPEG/PNG image loading |
| **nlohmann_json** | FetchContent | v3.11.3 | JSON (currently unused) |

Note: `BUILD_QC_ONLY` mode builds standalone QC tool without MINC/HDF5 dependencies.

---

## B.5 Testing

### Test Suite

| File | Tests | Coverage |
|------|-------|----------|
| `tests/csv_test.cpp` | 42 | CSVHandler: escaping, loading, saving, round-trips, edge cases |

All 42 tests pass. Tests create and clean up temporary CSV files.

### Coverage (Merged Structure)

| Module | Functions | Tested | Coverage |
|--------|-----------|--------|----------|
| CSVHandler | ~10 | ~10 | **~95%** |
| QCApp | ~15 | 0 | **0%** |
| QC Module Backends | ~20 | 0 | **0%** |
| main | 1 | 0 | **0%** |

**QCApp and backends are untested.** GUI code is inherently harder to unit-test, but `loadImage()`, `markAsPass/Fail()`, navigation logic, and auto-save could have isolated unit tests.

---

## B.6 Discrepancies (Pre-Merge)

| Item | README / CMakeLists says | Actual code |
|------|--------------------------|-------------|
| **nlohmann_json** | Listed as dependency | Not used anywhere in source |
| **Vulkan** | Listed in CMakeLists.txt `find_package` | Not used; only OpenGL used (pre-merge) |
| **stb_image** | Fetched via FetchContent | Used correctly |

**Post-merge:** Vulkan backend now implemented in `src/qc/VulkanBackend.cpp`.

---

## B.7 Potential Issues and Risks (Updated Post-Merge)

### HIGH
1. **No image prefetching**: Each navigation blocks on `stb_image_load()`. For large images over NFS this could cause UI stalls.
2. **Single texture recycled**: `currentImage_.textureId` is reused every navigation; if `loadImage()` fails, the old texture remains displayed without clear error to user.
3. **VulkanHelpers static globals** (merged): Device, queue, and pool are file-scope statics — same issue as new_register.

### MEDIUM
4. **Manual CLI parsing**: `main.cpp` hand-parses arguments; edge cases like `--scale=1.5` (vs `--scale 1.5`) are not handled.
5. **Notes field single-line**: ImGui `InputText` for notes is single-line; multi-line notes (with embedded newlines) are stored correctly in CSV but cannot be entered via UI.
6. **No progress autosave indicator**: User has no visual confirmation that auto-save occurred.
7. **Backend selection**: No test coverage for BackendFactory fallback logic.

### LOW
8. **No keyboard shortcut for notes focus**: Keyboard-only workflow breaks when entering notes.
9. **Case list scroll**: Uses `ImGui::SetScrollHereY()` to scroll to current row, but may not work correctly in all ImGui versions.
10. **HiDPI scaling**: `--scale` option works but no visual feedback for detected scale factor.

---

## B.8 Recommendations (Post-Merge)

### Immediate (Stability)
1. **WAITING**: Remove or wire up `nlohmann_json` — either use it for a config file or drop the dependency.
2. **DONE**: Backend abstraction merged with Vulkan support (commit a75fb1a).
3. Add visual auto-save indicator (e.g., brief status line: "Saved.").

### Short-term (Quality)
4. Add unit tests for `QCApp` navigation and QC marking logic (test-only init without GLFW).
5. Replace manual CLI parsing with `cxxopts` (already used in `new_register`).
6. Add image prefetching for previous/next cases (simple background load or at least on-advance pre-load).
7. Add tests for BackendFactory fallback logic.

### Medium-term (Features)
8. Multi-line notes input (`ImGui::InputTextMultiline`).
9. Keyboard shortcut to focus the notes field.
10. Filter/sort the case list (e.g., show only unrated).
11. Jump-to-first-unrated on startup (currently navigates to index 0 always).
12. HiDPI scale detection visual feedback in UI.

### Integration with new_register
13. Explore adding MINC2 volume support to QC tool (optional `BUILD_MINC_SUPPORT` mode).
