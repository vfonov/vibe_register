# research.md — Comprehensive Codebase Review

> **Last updated:** 2026-04-09 (NIfTI native support added + bugs fixed, x2go/SSH Vulkan auto-skip, WindowManager extracted, 21 test suites)
> **Scope:** Full source review of `new_register` (merged codebase: 27 `.cpp`, 21 headers in src/, 21 test suites).

---

# Part A — new_register

> **Original review date:** 2026-03-04

---

## 1. Project Summary

`new_register` is a modern C++17 rewrite of the legacy BIC `register` medical image
viewer/registration tool. It provides multi-volume MINC2 viewing with 3-plane slice
views, alpha-blended overlay compositing, tag-point registration (6 transform types),
QC batch review mode, and dual GPU backends (Vulkan + OpenGL 2).

**Codebase size:** ~14,250 lines across 27 `.cpp` + 21 `.h` files in `src/`, plus ~4,500 lines
of tests across 21 test suites.

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

- **Abstract backend**: `GraphicsBackend` provides a unified interface; `BackendFactory` selects at runtime with automatic fallback (Vulkan -> OpenGL2 -> OpenGL2-EGL). Vulkan is automatically skipped when running under x2go or SSH X11 forwarding (detected via `X2GO_SESSION`, `SSH_CONNECTION`, `SSH_CLIENT` env vars).
- **Synchronous prefetching**: Despite `Prefetcher` having thread-safety primitives, actual loading is main-thread-only because libminc/HDF5 are not thread-safe.
- **LRU volume cache**: `VolumeCache` (default 8 entries) avoids re-reading MINC files during QC row switches.
- **CPU compositing**: Overlay blending and colour mapping are done in software (not GPU shaders).
- **NIfTI native loading**: `NiftiVolume.cpp` implements the full `nii2mnc` algorithm so NIfTI world-space coordinates are identical to those of an equivalent MINC file.
- **WindowManager**: Framebuffer resize callback and swapchain rebuild flag are encapsulated in `WindowManager.cpp`, keeping `main.cpp` free from GLFW callback boilerplate.

---

## 3. Module-by-Module Analysis

### 3.1 main.cpp (1015 lines)

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

### 3.5 Interface.cpp (2914 lines) — Largest file

Full ImGui UI: dock layout, volume columns, overlay panel, tag list, QC verdicts, file dialogs, hotkey panel.

- `renderSliceView()` (335 lines): texture display, zoom/pan UV mapping, crosshairs, tag drawing, mouse interaction
- `renderOverlayView()` (246 lines): similar to slice view but for overlay
- `switchQCRow()` (84 lines): saves display settings, loads new volumes, restores, prefetches
- `renderToolsPanel()` (226 lines): sync checkboxes, QC controls, config I/O
- `renderVolumeColumn()` (366 lines): per-volume controls and 3 slice views
- `renderHotkeyPanel()` (new): displays keyboard shortcuts reference

**Strengths:** Comprehensive feature coverage; clean data/presentation separation.
**Concerns:** File is oversized (2914 lines — candidate for splitting). Significant code duplication between `renderSliceView()` and `renderOverlayView()`.

### 3.6 ViewManager.cpp (804 lines)

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

### 3.8 VulkanBackend.cpp (1096 lines) + VulkanHelpers.cpp (454 lines)

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

### 3.14 BackendFactory.cpp (99 lines)

Factory pattern for graphics backends with compile-time conditionals and string aliases.

- `detectBest()` skips Vulkan automatically when `isRemoteX11Display()` returns true.
- `isRemoteX11Display()` checks: `X2GO_SESSION` (x2go sessions), or `SSH_CONNECTION` / `SSH_CLIENT` combined with a non-empty `DISPLAY` (SSH X11 forwarding).
- Same logic applied in `src/qc/BackendFactory.cpp` (78 lines) for the `new_qc` backend.

**Rationale:** Vulkan over x2go / SSH X11 forwarding causes an uncatchable `XIO: fatal IO error 11` (SIGPIPE from the X server), which kills the process even with the existing SIGSEGV guard. OpenGL2-EGL is used instead and works correctly.

### 3.15 SliceRenderer.cpp (680 lines)

Slice rendering abstraction layer for multi-volume visualization.

- Manages slice orientation (sagittal, coronal, axial) and coordinate transforms
- Handles texture coordinate mapping for zoom/pan operations
- Crosshair rendering and world-coordinate synchronization

**Strengths:** Clean abstraction for slice rendering logic.
**Concerns:** Tightly coupled with ViewManager.

### 3.16 NiftiVolume.cpp (350 lines) — NEW (2026-04-03)

Native NIfTI-1 volume loader supporting `.nii` and `.nii.gz` files.

- Reads sform (preferred) or qform from the NIfTI header; falls back to pixdim diagonal.
- Replicates the `nii2mnc` algorithm in three stages:
  1. **Spatial axis permutation** — maps NIfTI file axes to MINC spatial dimensions by finding the largest absolute component of each affine column.
  2. **Decomposition** — extracts signed step, direction cosines, and start values via the `convert_transform_to_starts_and_steps()` logic (sign from diagonal element; start solved as `C⁻¹ · origin`).
  3. **Negative-step normalization** — flips steps to positive, shifts starts to the far end, and reverses voxel data along affected axes (replicating `minc2_setup_standard_order()`).
- Builds `voxelToWorld` / `worldToVoxel` from the decomposed MINC components — guaranteed consistent with `dirCos`, `step`, `start`.
- Converts meters/microns to millimeters via `xyz_units`.
- Supports DT_INT8, DT_UINT8, DT_INT16, DT_UINT16, DT_INT32, DT_UINT32, DT_FLOAT32, DT_FLOAT64.
- Applies `scl_slope` / `scl_inter` rescaling when present.
- Integrated into `Volume::load()` via `isNiftiFile()` extension check.

**Strengths:** Coordinate-identical to MINC for the same brain volume (validated by `NiftiMncMatchTest`).
**Concerns:** Full volume loaded into memory as float (same as MINC path). No lazy loading or memory mapping.

### 3.17 WindowManager.cpp (90 lines) — NEW

GLFW framebuffer callback management, extracted from `main.cpp`.

- Encapsulates `glfwSetFramebufferSizeCallback`, last-known framebuffer size, and `swapchainRebuildPending_` flag.
- Main loop polls `needsSwapchainRebuild()` / `resetRebuildFlag()` rather than accessing GLFW internals.

**Strengths:** Cleaner separation of GLFW lifecycle from main render loop.

### 3.18 QC Module (src/qc/) — MERGED

The `new_qc` tool was merged into `new_register/src/qc/` on 2026-03-16. See **Part B** for full analysis.

**Files added:**
- `qc/main.cpp` (153 lines): CLI entry point for standalone QC tool
- `qc/QCApp.cpp` (570 lines) + `qc/QCApp.h` (70 lines): GLFW+ImGui+OpenGL GUI application
- `qc/CSVHandler.cpp` (245 lines) + `qc/CSVHandler.h` (58 lines): CSV I/O with RFC 4180 compliance
- `qc/VulkanBackend.cpp` (945 lines) + `qc/VulkanBackend.h` (82 lines): Vulkan GPU backend for QC
- `qc/VulkanHelpers.cpp` (378 lines) + `qc/VulkanHelpers.h` (33 lines): Vulkan helper functions
- `qc/OpenGL2Backend.cpp` (318 lines) + `qc/OpenGL2Backend.h` (57 lines): OpenGL2 fallback backend
- `qc/Backend.h` (136 lines): Abstract backend interface
- `qc/BackendFactory.cpp` (78 lines): Backend selection factory (includes x2go/SSH Vulkan skip)

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
| `NiftiLoadTest` | test_nifti_load.cpp | NIfTI volume loading dimensions and world-coord sanity |
| `NiftiMncMatchTest` | test_nifti_mnc_match.cpp | NIfTI and MINC produce identical slices for the same brain volume |
| `QCCsvTest` | test_qc_csv.cpp | CSV parsing, quoting, round-trip, edge cases |
| `AppConfigTest` | test_app_config.cpp | JSON config round-trip, defaults, optional fields |
| `TransformTest` | test_transform.cpp | All transform types, XFM I/O |
| `MincpikTest` | test_mincpik.cpp | Mosaic image generation |
| `QCCsvHandlerTest` | csv_test.cpp | CSVHandler unit tests (42 tests) |
| `OverlapTest` | test_overlap.cpp | Overlay rendering with non-identity direction cosines |
| `Overlap2Test` | test_overlap2.cpp | Overlay rendering with identity direction cosines (baseline) |
| `CoordSq2TrTest` | (implicit) | Coordinate transform for sq2_tr volume |

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

**Approximate overall coverage of unit-testable functions: ~38% (improved with NIfTI and CSVHandler tests).**

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
- BackendFactory name parsing / x2go detection logic
- SliceRenderer unit tests
- NiftiVolume edge cases (non-square voxels, qform-only, all datatype paths)

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
| **LSP configuration** | Manual `-I` flags in `.clangd` | `compile_commands.json` auto-generated by CMake |

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
- 21 passing test suites (including OverlapTest, NiftiMncMatchTest)
- WARN verdict system with configurable verdict options and 1..N hotkeys
- clangd LSP configuration with auto-generated compile commands
- **NIfTI-1 support** (`.nii`, `.nii.gz`) via native loader replicating the `nii2mnc` algorithm — coordinate-identical to MINC for the same volume (2026-04-03/05)
- **Automatic Vulkan skip on x2go / SSH X11 forwarding** — `detectBest()` detects remote X11 via env vars and falls through to OpenGL2-EGL; applies to both `new_register` and `new_qc` (2026-04-09)

---

## 9. Not Yet Implemented

From PLAN.md — features remaining from the legacy tool:

- **Resampling**: Resample volume 2 into volume 1's space using computed transform
- **Slice filtering**: Per-view filter type (nearest, linear, box, triangle, gaussian) with configurable FWHM
- **Interpolation**: Per-volume interpolation type selection
- **Additional volume support**: Up to 8 volumes, RGB/vector volumes, 4D volumes (NIfTI-1 is now supported)
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
12. RGB/vector volume support and 4D volumes.
13. Metal backend for macOS.

---

## 11. NIfTI Coordinate System Analysis

> **Date:** 2026-04-05 (analysis); **Fixed:** 2026-04-05 (commit 545f1a5)
> **Scope:** Mathematical analysis of NIfTI vs MINC coordinate representations,
> the `nii2mnc` conversion algorithm, and bugs that existed in the initial `NiftiVolume.cpp` loader (all now resolved).

### 11.1 NIfTI Coordinate Representation

NIfTI stores a 4×4 affine matrix `A` (the **sform** or **qform**) that maps
integer voxel indices to world coordinates in **RAS**
(Right-Anterior-Superior) orientation:

```
┌ x ┐       ┌ a₀₀  a₀₁  a₀₂  t₀ ┐   ┌ i ┐
│ y │   =   │ a₁₀  a₁₁  a₁₂  t₁ │ · │ j │
│ z │       │ a₂₀  a₂₁  a₂₂  t₂ │   │ k │
└ 1 ┘       └  0    0    0    1  ┘   └ 1 ┘
```

- `(i, j, k)` = integer voxel indices along the three file axes.
- `(x, y, z)` = world coordinates: x = Right/Left, y = Anterior/Posterior,
  z = Superior/Inferior.
- The 3×3 upper-left encodes rotation + scaling.  Column `j` of this
  submatrix is the world-space direction vector for file axis `j`, scaled
  by the voxel spacing along that axis.
- `(t₀, t₁, t₂)` = translation = world position of voxel `(0, 0, 0)`.
- sform is preferred over qform when both are present.
- `nii_ptr->xyz_units` declares spatial units (mm, meters, or microns).

### 11.2 MINC Coordinate Representation

MINC decomposes the equivalent affine into three **per-axis** scalar/vector
attributes for each spatial dimension `d ∈ {xspace, yspace, zspace}`:

| Attribute | Symbol | Type | Meaning |
|-----------|--------|------|---------|
| `direction_cosines` | **cₐ** | 3-vector (unit) | World direction of axis `d` |
| `step` | **sₐ** | signed scalar | Voxel spacing (mm); sign encodes axis orientation |
| `start` | **oₐ** | scalar | Position of voxel 0 *projected onto* direction **cₐ** |

MINC also uses a RAS-like world coordinate system: xspace = R/L, yspace = A/P,
zspace = S/I.  There is no sign flip between NIfTI and MINC world coordinates.

**Reconstruction formula** — MINC rebuilds the 4×4 voxel-to-world matrix as:

```
Column d of M₃ₓ₃ = cₐ · sₐ     (direction cosine scaled by step)
Translation T     = C · o        (where C = [c_x | c_y | c_z], o = [o_x, o_y, o_z]ᵀ)
```

Or written out:

```
        ┌ c_x[0]·s_x   c_y[0]·s_y   c_z[0]·s_z ┐
M₃ₓ₃ = │ c_x[1]·s_x   c_y[1]·s_y   c_z[1]·s_z │
        └ c_x[2]·s_x   c_y[2]·s_y   c_z[2]·s_z ┘

        ┌ c_x[0]·o_x + c_y[0]·o_y + c_z[0]·o_z ┐
T     = │ c_x[1]·o_x + c_y[1]·o_y + c_z[1]·o_z │
        └ c_x[2]·o_x + c_y[2]·o_y + c_z[2]·o_z ┘
```

The full transform is:  **world = M₃ₓ₃ · voxel + T**

**Critical point:** `start` is NOT the world coordinate of voxel 0.  It is the
scalar projection of the origin onto the direction cosine basis.  The actual
world position of voxel 0 is `T = C · o`, which mixes all three start values
through the direction cosine matrix.

### 11.3 The nii2mnc Conversion Algorithm

The canonical conversion (implemented identically in both
`legacy/minc-tools/conversion/nifti1/nii2mnc.c` lines 379–636 and
`legacy/libminc/volume_io/Volumes/input_nifti.c` lines 130–267) has
three stages:

#### Stage 1 — Spatial Axis Permutation (`nii2mnc.c` lines 417–430)

NIfTI file axes `(i, j, k)` can be in any order relative to world axes.
For each file axis `j ∈ {0, 1, 2}`, determine which MINC spatial dimension
it most closely aligns with:

```
spatial_axes[j] = argmax over d ∈ {0,1,2} of |A[d][j]|
```

This examines column `j` of the affine (the world-space direction vector for
file axis `j`) and picks the world axis with the largest absolute component.

Result: `spatial_axes[j]` = the MINC spatial axis index (0=xspace, 1=yspace,
2=zspace) that NIfTI file axis `j` maps to.

#### Stage 2 — Column Permutation (`nii2mnc.c` lines 433–438)

Rearrange the columns of `A` into a new matrix `M_perm` so that each column
corresponds to its MINC spatial axis:

```
M_perm[row][spatial_axes[j]] = A[row][j]     for row ∈ {0,1,2}, j ∈ {0,1,2}
M_perm[row][3]               = A[row][3]     (translation copied directly)
```

After this, column 0 of `M_perm` is the xspace direction vector, column 1 is
yspace, column 2 is zspace — regardless of the original NIfTI file storage
order.

This permutation also reorders the dimension names so that the MINC file
dimensions match the permuted columns.

#### Stage 3 — Decomposition via `convert_transform_to_starts_and_steps()`

This function (`legacy/libminc/volume_io/Volumes/volumes.c` lines 1252–1324)
decomposes the permuted 4×4 matrix into MINC's (dirCos, step, start) triplets.

**Step 3a — Extract columns and origin:**

```
axes[d] = column d of M_perm     (3-vector, for d = 0, 1, 2)
origin  = column 3 of M_perm     (3-vector = translation)
```

Each `axes[d]` is the product `dirCos[d] × step[d]` — a direction cosine
vector scaled by the step size.

**Step 3b — Compute step magnitude:**

```
mag_d = ‖axes[d]‖₂ = √(axes[d] · axes[d])
```

**Step 3c — Determine step sign** (when `step_signs` is NULL, which is the
`nii2mnc` case):

```
sign_d = { −1   if axes[d][d] < 0
          { +1   otherwise
```

The sign is determined by the **diagonal element** `axes[d][d]` — the
component of the d-th axis vector along the d-th world direction.  If the
xspace direction vector has a negative X component, the step is negative.

This convention ensures that the direction cosines, after normalization,
have a positive diagonal (i.e., each axis mostly points in the positive
direction of its corresponding world axis).

**Step 3d — Compute step and direction cosines:**

```
step[d]   = sign_d × mag_d
dirCos[d] = axes[d] / step[d]
```

Dividing by the **signed** step normalizes the vector to unit length and
flips it if the step is negative.  The result is `‖dirCos[d]‖ = 1` with
`dirCos[d][d] > 0` (positive diagonal).

**Step 3e — Compute starts** via `convert_transform_origin_to_starts()`
(`volumes.c` lines 1146–1231):

Solve the 3×3 linear system:

```
C · o = origin

where C = [dirCos[0] | dirCos[1] | dirCos[2]]   (columns)
      o = [start_x, start_y, start_z]ᵀ           (unknowns)
      origin = translation from column 3
```

This is solved via Gaussian elimination (`solve_linear_system()`).

**Step 3f — Unit conversion** (`nii2mnc.c` lines 590–609):

Scale `starts` and `steps` from NIfTI's declared units to millimeters:
- meters → multiply by 1000
- microns → divide by 1000
- mm → no change

### 11.4 Complete Algorithm Summary

```
NIfTI file
    │
    ▼
1. Read sform (preferred) or qform → mat44 A
   A.m[row][col], rows = world XYZ, cols = file axes i,j,k + translation
    │
    ▼
2. Permute: for each file axis j, find spatial_axes[j] = argmax_d |A[d][j]|
    │
    ▼
3. Rearrange columns: M_perm[row][spatial_axes[j]] = A[row][j]
   Now column d of M_perm = direction for MINC spatial axis d
    │
    ▼
4. Decompose M_perm:
   a. axes[d] = column d               (scaled direction vector)
   b. mag_d   = ‖axes[d]‖              (absolute step size)
   c. sign_d  = sgn(axes[d][d])        (from diagonal element)
   d. step[d] = sign_d × mag_d         (signed step)
   e. dirCos[d] = axes[d] / step[d]    (unit direction cosine)
   f. Solve C·o = origin for starts    (3×3 linear system)
    │
    ▼
5. Scale starts and steps to mm if needed
    │
    ▼
6. Write step[d], dirCos[d], start[d] to MINC xspace/yspace/zspace
```

### 11.5 Key Mathematical Identity

The decomposition is exact and invertible.  Given the MINC components, the
original permuted affine is perfectly reconstructed:

```
M_perm = C · diag(step)      (columns 0–2)
origin = C · start            (column 3)
```

And: `world = C · diag(step) · voxel + C · start = C · (diag(step) · voxel + start)`

### 11.6 Bugs in the Initial NiftiVolume.cpp (All Fixed — commit 545f1a5)

The initial loader (`new_register/src/NiftiVolume.cpp`, commit ff875ce)
had six distinct bugs, all corrected in commit 545f1a5:

#### Bug 1: No spatial axis permutation (Stages 1–2 missing)

The code uses the raw NIfTI affine columns directly as if file axis 0 = xspace,
axis 1 = yspace, axis 2 = zspace.  For volumes whose file axes don't align with
world axes in this canonical order (which is common — many NIfTI files store
data in a different axis order), the direction cosines and steps are assigned to
the wrong MINC spatial dimensions.

This is the permutation performed at `nii2mnc.c` lines 417–438 that the
current code entirely omits.

#### Bug 2: No step sign convention

The step is always computed as the positive column magnitude (`len`), with no
check of the diagonal element `axes[d][d]`.  MINC requires the step sign to
match the convention in `convert_transform_to_starts_and_steps()`: negative
when the diagonal is negative.  Without this, the direction cosines are not
normalized to have positive diagonals, which breaks the MINC contract.

#### Bug 3: Incorrect start computation

The code sets `start = transformVoxelToWorld(0,0,0)`, which gives the raw
world coordinate of voxel (0,0,0).  But MINC's `start` is the scalar projection
of the origin onto the direction cosine basis — it must be computed by solving
the linear system `C · o = origin`.  The two are only equal when direction
cosines are the identity matrix.

#### Bug 4: Inconsistent voxelToWorld construction

The `voxelToWorld` matrix is built directly from the raw NIfTI affine
(lines 129–142) rather than being reconstructed from the decomposed MINC
components using the formula `M = C · diag(step)`, `T = C · start`.  This
means `voxelToWorld` doesn't match the stored dirCos/step/start — the two
representations are inconsistent with each other.

#### Bug 5: Double unit_scale on step

`vol.step` is set to `len * unit_scale` on line 117, where `len` is the column
magnitude from the NIfTI affine.  The affine columns already encode voxel
spacing in NIfTI's declared units.  Meanwhile, the 3×3 rotation/scaling columns
of `voxelToWorld` (lines 129–132) use the raw affine values without unit
scaling.  For the common case of mm (`unit_scale = 1.0`) this is harmless, but
for meters or microns the step would be scaled while the matrix wouldn't,
creating an inconsistency.

#### Bug 6: GLM column-major transposition pitfall

The `glm::dmat3` constructor at lines 96–100 fills column-major: the first
three arguments become column 0, not row 0.  Since `nii_xfm.m[row][col]` is
row-major, the resulting `affine` matrix is the **transpose** of the NIfTI 3×3.
The code works around this on line 112 by extracting elements as
`(affine[0][axis], affine[1][axis], affine[2][axis])`, which happens to recover
the correct NIfTI column.  But the `affine` matrix itself is wrong and must not
be used for matrix-vector multiplication.  Lines 129–132 do use individual
element access that compensates for the transposition, so the `voxelToWorld`
rotation part is accidentally correct.  This is fragile and confusing.

### 11.7 Test Validation

`NiftiMncMatchTest` (test #15) loads the same brain volume as both NIfTI
(`clp_VF_20190508_t1.nii.gz`) and MINC (`clp_VF_20190508_t1.mnc`), renders
central axial/sagittal/coronal slices from each, and compares pixel data.

**Before fix (commit ff875ce):**
- Corner (0,0,0) world coordinates differed by **388.8 mm**
  (NIfTI: 114.7, 121.6, 99.4 vs MINC: −105.4, −92.7, −138.8)
- **31,000–40,000 pixel mismatches** per slice

**After fix (commit 545f1a5):**
- Zero pixel mismatches across all three slice views.
- `NiftiMncMatchTest` passes as part of the 21-suite CTest run.

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
