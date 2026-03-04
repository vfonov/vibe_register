# research.md — new_register Comprehensive Codebase Review

> **Date:** 2026-03-04
> **Scope:** Full source review of all 15 source files, 15 headers, and 14 test suites.

---

## 1. Project Summary

`new_register` is a modern C++17 rewrite of the legacy BIC `register` medical image
viewer/registration tool. It provides multi-volume MINC2 viewing with 3-plane slice
views, alpha-blended overlay compositing, tag-point registration (6 transform types),
QC batch review mode, and dual GPU backends (Vulkan + OpenGL 2).

**Codebase size:** ~8,400 lines across 15 `.cpp` + 15 `.h` files, plus ~2,700 lines
of tests across 14 test suites.

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

### 3.1 main.cpp (800 lines)

Entry point, CLI parsing (via `cxxopts`), GLFW window creation, main render loop.

- Backend selection with multi-level fallback (Vulkan -> OpenGL2 -> OpenGL2-EGL)
- DPI-aware window sizing with monitor workarea clamping
- QC mode initialization with CSV input/output and prefetching
- Main loop: poll events -> prefetch one volume -> check swapchain -> ImGui frame -> render -> present

**Strengths:** Robust backend fallback; clean QC initialization.
**Concerns:** `main()` is ~600 lines and could benefit from extraction into setup helpers.

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

### 3.5 Interface.cpp (2273 lines) — Largest file

Full ImGui UI: dock layout, volume columns, overlay panel, tag list, QC verdicts, file dialogs.

- `renderSliceView()` (335 lines): texture display, zoom/pan UV mapping, crosshairs, tag drawing, mouse interaction
- `renderOverlayView()` (246 lines): similar to slice view but for overlay
- `switchQCRow()` (84 lines): saves display settings, loads new volumes, restores, prefetches
- `renderToolsPanel()` (226 lines): sync checkboxes, QC controls, config I/O
- `renderVolumeColumn()` (366 lines): per-volume controls and 3 slice views

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

### 3.8 VulkanBackend.cpp (1056 lines) + VulkanHelpers.cpp (406 lines)

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

**Approximate overall coverage of unit-testable functions: ~30%.**

### Coverage Gaps (Priority Order)

**HIGH — AppState (0%):**
- `VolumeCache` get/put/eviction
- `getTagPairs()`, `applyConfig()`, `initializeViewStates()`
- `saveTags()` routing, `loadCombinedTags()`, `saveCombinedTags()`

**MEDIUM — TagWrapper mutations:**
- `removeTag()`, `updateTag()`, `clear()`

**MEDIUM — Volume utilities:**
- `get()` bounds check, `worldExtent()`, `slicePixelAspect()`, `generate_test_data()`

**LOW:**
- Copy/move semantics (Volume, TagWrapper)
- Prefetcher integration test
- BackendFactory name parsing

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
