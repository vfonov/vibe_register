# AGENTS.md — Agent Operating Guide

This is the single authoritative reference for AI agents working in this repository.
Read this file before making any changes.

---

## 1. Repository Overview

| Directory | Description |
|-----------|-------------|
| `legacy/register/` | Original BIC `register` C tool — **read-only reference, do not modify** |
| `legacy/bicgl/` | Original BIC Graphics Library — **read-only reference, do not modify** |
| `new_register/` | Modern C++17 rewrite of `register` (Vulkan + ImGui + MINC2) — includes `new_qc` viewer |

**Key documentation files:**

| File | What it covers |
|------|----------------|
| [`research.md`](research.md) | Deep codebase review — architecture diagrams, module analysis, test coverage, risks |
| [`PLAN.md`](PLAN.md) | Feature roadmap and remaining work items |
| [`problem.md`](problem.md) | Known open bugs requiring investigation |
| [`new_register/README.md`](new_register/README.md) | User-facing usage documentation for new_register and new_qc |

**Dependencies (new_register):**
- `minc2-simple` (FetchContent from GitHub, develop branch)
- `ImGui` (fetched via CMake, Docking branch)
- `GLFW` (system)
- `Vulkan` (system, optional — OpenGL2 fallback available)
- `nlohmann/json` (v3.11.3, FetchContent)
- `Eigen` (FetchContent, for transform math)

---

## 2. new_register — Source Layout

```
new_register/
├── CMakeLists.txt              Build: nr_core lib + new_register exe + new_mincpik exe
├── src/
│   ├── main.cpp                CLI parsing (cxxopts), GLFW/backend init, render loop
│   ├── AppConfig.cpp           JSON config load/save (nlohmann/json)
│   ├── AppState.cpp            Volume lifecycle, LRU cache, tag management, transform dispatch
│   ├── Interface.cpp           All ImGui UI — slice views, QC panels, tag list, hotkeys
│   ├── ViewManager.cpp         Texture generation, overlay compositing, cursor sync
│   ├── Volume.cpp              MINC2 file loading (minc2-simple), voxel I/O, world↔voxel transforms
│   ├── Transform.cpp           Tag-based registration: LSQ6/7/9/10/12 + TPS (Eigen SVD)
│   ├── ColourMap.cpp           21 colour map LUTs (piecewise-linear, 256 entries)
│   ├── SliceRenderer.cpp       CPU slice extraction and colour mapping (used by new_mincpik too)
│   ├── QCState.cpp             QC batch mode: RFC 4180 CSV parsing, verdict tracking
│   ├── TagWrapper.cpp          .tag file I/O (minc2-simple C API wrapper)
│   ├── Prefetcher.cpp          Main-thread-only volume prefetching for QC row switches
│   ├── VulkanBackend.cpp       Vulkan GPU backend (device, swapchain, textures, screenshots)
│   ├── OpenGL2Backend.cpp      OpenGL 2.1 fallback backend (for SSH/software renderers)
│   ├── VulkanHelpers.cpp       Shared Vulkan state (device, queues, command pools)
│   └── BackendFactory.cpp      Runtime backend selection with fallback chain
├── include/                    Headers mirroring src/ structure
└── tests/                      16 CTest suites + debug tools
```

**Legacy layout (reference only — do not modify):**

```
legacy/
├── register/       Original C register application
│   ├── User_interface/
│   └── Functionality/
└── bicgl/          BIC Graphics Library
    ├── OpenGL_graphics/
    └── GLUT_windows/
```

---

## 3. Build & Test

```bash
cd new_register/build
cmake ..
make -j$(nproc)
ctest --output-on-failure
```

Run a specific test by regex:
```bash
ctest -R OverlapTest
```

**Debug tools** (built with `make`, run manually — not ctests):

| Binary | Usage | Purpose |
|--------|-------|---------|
| `tests/dump_vol` | `dump_vol <file.mnc> [...]` | Print dims, step, start, dirCos, voxelToWorld, corner world coords |
| `tests/generate_overlap_refs` | `generate_overlap_refs <sq1.mnc> <sq2_tr.mnc> <out_dir>` | Render and save reference PNGs for OverlapTest |

---

## 4. Architecture Notes

- `nr_core` static library bundles GPU-agnostic code (Volume, Transform, AppConfig, SliceRenderer, etc.) for reuse by `new_mincpik`.
- QC mode: `--qc input.csv --qc-output results.csv`; input columns are volume paths; verdicts written per-column.
- **Backend fallback chain:** Vulkan → OpenGL2 → OpenGL2-EGL. `BackendFactory.cpp` drives selection; `--backend` CLI flag overrides.
- Prefetcher runs on the **main thread only** (libminc/HDF5 not thread-safe).
- `Interface.cpp` (~2300 lines) is the largest file and a refactoring candidate.

### Graphics Backend

- `GraphicsBackend` (abstract base) defines the interface: `createTexture`, `updateTexture`, `beginFrame`, `endFrame`, `captureScreenshot`, etc.
- `VulkanBackend.cpp` — primary; uses ImGui's Vulkan backend + custom helpers. Texture uploads use a `VkFence` (not `vkQueueWaitIdle`).
- `OpenGL2Backend.cpp` — fallback for SSH / software renderers.
- `VulkanHelpers.cpp` — persistent staging buffer + dedicated upload command pool.

### Window & Dock Layout

- GLFW `framebufferCallback` triggers swapchain rebuild on resize.
- `Interface::render()` rebuilds the ImGui dock layout from scratch whenever the viewport size changes. All splits are fractional (proportional resize).
- Tools panel fraction: 1 column → 28%, 2 → 18%, 3 → 14%, 4+ → 11% (QC mode adds +2%).
- Content columns (volumes + optional Overlay) are split equally in the remaining space.

---

## 5. Implemented Features

- MINC2 volume loading via `minc2-simple`, multi-volume side-by-side display
- Three orthogonal slice views per volume (axial, sagittal, coronal)
- Crosshair navigation with optional cursor sync across volumes
- 21 colour maps, per-volume display range, zoom/pan per view
- Overlay compositing (volume 2 alpha-blended over volume 1)
- Tag point placement, editing, and `.tag` file I/O
- Registration transforms: LSQ6/7/9/10/12 and TPS (Eigen SVD)
- QC batch review mode (see Section 8)
- Screenshot capture (`P` key → `screenshot000001.png`, auto-incrementing)
- JSON config persistence (`config.json`)
- Hotkey reference panel in the UI

---

## 6. Coding Standards

### new_register (C++17)

| Aspect | Rule |
|--------|------|
| Standard | C++17 (`CMAKE_CXX_STANDARD 17`); `std::vector`, `std::string`, smart pointers are fine |
| Classes | `PascalCase` |
| Methods / variables | `camelCase` |
| Namespace | `QC::` for all code in `src/qc/` |
| Error output | `std::cerr` — never `printf` to stderr |
| Braces | Allman style (opening brace on its own line) |
| Indentation | 4 spaces, no tabs |

**Hotkey panel rule:** When adding, removing, or changing any hotkey or mouse interaction, always update `Interface::renderHotkeyPanel()` to keep the panel accurate.

**Never touch `legacy/`.** Use it only as a read-only reference.

### legacy/ (C, read-only reference)

| Aspect | Rule |
|--------|------|
| Variables / functions | `snake_case` |
| Types / structs | `PascalCase` or `Capitalized_Snake_Case` |
| Macros / constants | `UPPER_SNAKE_CASE` |
| Error output | `print_error()` or `fprintf(stderr, ...)` — never `printf` |
| Memory | Every `malloc` must have a corresponding `free`; use `FREE()` macro if available |

Include order (both projects): system headers → library headers → local headers.

---

## 7. Workflow Rules

1. **Read before writing** — read the full function you are modifying; check `CMakeLists.txt` for dependencies.
2. **Legacy is read-only** — do not modify anything under `legacy/`.
3. **C++17 for new code** — use modern C++ features; do not back-port to C-style.
4. **Compile after every change** — `make` must succeed before moving on.
5. **Run tests** — if modifying logic, run `ctest --output-on-failure`.
6. **No reformatting** — follow the existing style of the file you are editing; do not reformat unrelated lines.
7. **Lint only what you touch** — `cppcheck` or `clang-tidy` on changed files only.
8. **Use LSP for code search** — prefer the LSP tool (go-to-definition, find-references, hover) over `grep`/`rg` when searching for symbols, definitions, or usages in `new_register/`.

---

## 8. QC Modes

### 8a. `new_register --qc` (Integrated MINC QC)

Full MINC2 volume QC embedded in `new_register`. Implemented in `QCState.cpp` and `Interface.cpp`.

```bash
./new_register --qc input.csv --qc-output results.csv [--config qc_config.json]
# Single-verdict mode (one verdict per row, not per column):
./new_register --qc1 input.csv --qc-output results.csv
```

**CSV format:** Input — first column `ID`, remaining columns are MINC2 volume file paths. Output — `<name>_verdict` and `<name>_comment` per input column.

**Design notes:**
- Verdict state lives in `QCState` (not `AppState`); `Interface` polls it.
- Volumes are loaded by `Prefetcher` on the main thread (libminc/HDF5 not thread-safe).
- Tag creation and tag list are disabled in QC mode.
- Output CSV is auto-saved on every verdict change.
- Navigate with `[` / `]` keys or by clicking rows in the QC list.
- On launch, existing output CSV is loaded and the viewer jumps to the first unreviewed row.

### 8b. `new_qc` (Standalone Image QC Viewer)

Lightweight standalone viewer for reviewing arbitrary images (PNG, JPG, etc.) from a CSV manifest.

```bash
./new_qc input.csv output.csv [--scale <factor>]
```

**CSV:** Input — `id,visit,picture`. Output — `id,visit,picture,QC,notes`.

**Keyboard:** `P` Pass (auto-advance), `F` Fail (auto-advance), `←`/`Page Up` previous, `→`/`Page Down` next, `Ctrl+S` save, `Esc` exit.

**Structure:** `new_register/src/qc/` — `QCApp`, `CSVHandler`, own Vulkan/OpenGL2 backends.

---

## 9. C++23 Modernization (Future)

The codebase targets C++17. A future upgrade to C++23 is planned. Phases 1–2 (unique_ptr, std::array) are complete. Remaining work is blocked on the C++23 upgrade:

- **Phase 3:** `std::views::iota`, `std::ranges::minmax_element` in ColourMap.cpp / Volume.cpp
- **Phase 4:** `constexpr` / `consteval` for LUT helpers; replace `countOf` template with `std::size()`
- **Phase 5:** Structured bindings in AppConfig.cpp, TagWrapper.cpp, main.cpp
- **Phase 6:** Replace magic numbers (hardcoded `256`, `sizeof`/`sizeof` ratios)
- **Phase 7:** `std::format` / `std::print` replacing `printf`/`fprintf`/`std::cout`

---

## 10. Known Open Issues

See [`problem.md`](problem.md) for full details.

**OverlapTest** — `renderOverlaySlice()` in `SliceRenderer.cpp` produces incorrect world-space resampling for volumes with non-identity direction cosines (`sq2_tr.mnc`). The reference PNGs are ground truth generated by an external tool (nearest-neighbor resample + `new_mincpik` render). The bug is in the coordinate transform chain (`voxelToWorld` / `worldToVoxel` construction in `Volume.cpp`). `Overlap2Test` (identity dirCos) passes.
