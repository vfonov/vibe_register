# AGENTS.md — Agent Operating Guide

Rules and orientation for AI agents working in this repository. Read before making changes.
User-facing material (features, CLI flags, config/CSV formats, build options, dependency
background) lives in [`new_register/README.md`](new_register/README.md).

---

## Repository map

| Path | What it is |
|------|------------|
| `legacy/` | Original BIC `register`, `bicgl`, and related C sources — **read-only reference** |
| `new_register/` | C++17 rewrite (ImGui + Vulkan/OpenGL2; Metal on macOS). Builds the `nr_core` library and the `new_register`, `new_mincpik`, `new_qc` executables |
| `new_register/mriv/` | Terminal slice viewer built on `nr_core`, plus `mriv_qc` (terminal image QC; links `mriv_term` + `qc_csv`, not `nr_core`). **Read [`new_register/mriv/CLAUDE.md`](new_register/mriv/CLAUDE.md) before working there** — it has its own rules (TDD, cxxopts, reuse boundary) |

Other docs: [`PLAN.md`](PLAN.md) — roadmap, including the C++23 migration ·
[`problem.md`](problem.md) — record new bugs here.

---

## Hard rules

1. **`legacy/` is read-only.** Use it as reference only; never modify anything under it.
2. **C++17 only** (`CMAKE_CXX_STANDARD 17`). Use modern C++, not C-style code. The C++23 upgrade is planned, not started — don't jump ahead.
3. **Read before writing** — read the whole function you are changing; check `CMakeLists.txt` for how a file is built.
4. **Compile after every change** (`make` must succeed); **run `ctest --output-on-failure`** whenever logic changes.
5. **No reformatting** — match the file's existing style and leave unrelated lines alone. Lint (`cppcheck` / `clang-tidy`) only the files you changed.
6. **Prefer the LSP tool** (go-to-definition, find-references, hover) over `grep` for symbol search in `new_register/`.
7. **No absolute paths in source or tests.** Pass paths as arguments; use `CMAKE_CURRENT_SOURCE_DIR` for test data paths in CMake.
8. **Hotkeys:** any change to a hotkey or mouse interaction must update `Interface::renderHotkeyPopup()` and the shortcut tables in `new_register/README.md`.
9. **libminc/HDF5 are not thread-safe** — volume loading, including `Prefetcher`, stays on the main thread.
10. **CLI flags go through `CliArgs.{h,cpp}`** so `main.cpp` and `main_macos.mm` stay in sync; update `printUsage()` and the README options table with them.
11. **Don't reorder dependency detection** in `new_register/CMakeLists.txt` — see below.

---

## Build & test

```bash
cd new_register/build
cmake ..
make -j$(nproc)
ctest --output-on-failure    # all tools enabled: 76 suites — 48 from tests/, headless_ui_tests, 27 mriv_*
ctest -R OverlapTest         # one test by regex; -R "^mriv_" for the subproject
```

- **Tool selection:** `BUILD_NEW_REGISTER`, `BUILD_NEW_QC`, `BUILD_NEW_MINCPIK`, `BUILD_TERMINAL_VIEWER` (all ON). In `CMakeLists.txt` they derive `NR_NEED_CORE` (MINC stack, Eigen, glm, json → `nr_core`) and `NR_NEED_GUI` (GLFW, Wayland, Vulkan, OpenGL, ImGui). Put any new dependency lookup or fetch under the matching gate, so disabled tools never download it.

- **Adding a test:** Catch2 v3 `TEST_CASE`s (no `main()`), built with `add_nr_test(<name> CATCH2 ...)` (`new_register/tests/CMakeLists.txt`) and registered with `add_test()`. `CATCH2` links `nr_test_main`, which supplies `main()` plus `--data-dir` / `--src-dir` options read via `tests/nr_test.h`. Older suites are still plain `assert` / hand-rolled checks in a `main()` and are being migrated (`PLAN.md`). No other test framework.
- Catch2 is resolved in `new_register/CMakeLists.txt` under `ENABLE_TESTS`, just before `add_subdirectory(tests)` — system package preferred, otherwise FetchContent at a pinned release tag.
- Anything `tests/` links must be found **before** `add_subdirectory(tests)`. A `find_library` placed after it yields an empty variable on a clean configure and only appears to work on re-configure.
- **macOS code cannot be compiled on Linux.** On `APPLE`, CMake builds `src/main_macos.mm` + `src/MetalBackend.mm` (ARC) instead of `main.cpp` / `WindowManager.cpp`, and `new_qc` uses `src/qc/main_macos.mm` + `src/qc/MetalBackend.mm`; OSMesa tests are disabled. A green Linux build says nothing about those files — keep changes to shared code (`GraphicsBackend`, `Interface`, `CliArgs`) compatible with both entry points.

**Debug tools** (built by `make`, run manually — not ctests):

| Binary | Usage | Purpose |
|--------|-------|---------|
| `tests/dump_vol` | `dump_vol <file.mnc> [...]` | Print dims, step, start, dirCos, voxelToWorld, corner world coords |
| `tests/generate_overlap_refs` | `generate_overlap_refs <sq1.mnc> <sq2_tr.mnc> <out_dir>` | Render and save reference PNGs for OverlapTest |
| `generate_headless_baselines` | `generate_headless_baselines <tests_dir>` | Regenerate baseline PNGs for the headless UI tests |
| `tests/generate_test_mode_refs` | `generate_test_mode_refs <out_dir>` | Regenerate reference images for TestModeRenderTest |

---

## Dependency detection — the order is load-bearing

CMake locates a minc-toolkit (`MINC_TOOLKIT_DIR`, `$MINC_TOOLKIT`, then `/opt/minc/*`) **before**
resolving HDF5. When one is found, HDF5/NetCDF/zlib are pinned to the toolkit's copies, and the
pins are passed explicitly into the `minc2-simple` and `libminc` sub-builds (`-DHDF5_ROOT`,
`-DHDF5_C_INCLUDE_DIR`, `-DHDF5_hdf5_LIBRARY_RELEASE`). Break that and toolkit and system HDF5
headers mix, producing a wall of `redeclaration of enumerator 'H5_ITER_UNKNOWN'` errors from
`minc2-simple`. Background: README, *Building against minc-toolkit*.

- libminc is resolved from the toolkit prefix by hand. Don't switch to the `LIBMINC::minc2` target or `UseLIBMINC.cmake` — a relocated toolkit's exported config bakes in the original build machine's absolute paths.
- Check what a build actually picked up:
  ```bash
  cmake .. 2>&1 | grep -E "MINC toolkit|HDF5|zlib|NETCDF"
  ldd new_register | grep -E "hdf5|minc|libz"
  ```

---

## Where things live (`new_register/`)

```
src/main.cpp              GLFW + backend init, render loop (non-macOS entry point)
src/main_macos.mm         macOS-only Cocoa entry point (imgui_impl_osx)
src/MetalBackend.mm       macOS-only Metal renderer (imgui_impl_metal)
src/CliArgs.cpp           Shared hand-rolled argv parsing (in nr_core)
src/AppState.cpp          Volume lifecycle, LRU cache, tags, transform dispatch
src/Interface.cpp         ALL ImGui UI (~3000 lines; refactoring candidate): views, QC panels, tag list, hotkey panel
src/ViewManager.cpp       Texture generation, overlay compositing, cursor sync
src/Volume.cpp            Volume::load() — dispatches to MINC2 (minc2-simple) or NIfTI (NiftiVolume.cpp) by extension
src/VolumeReorient.cpp    Shared file-order → Volume::data reorientation (MINC and NIfTI loaders)
src/SliceRenderer.cpp     CPU slice extraction + colour mapping (shared with new_mincpik and mriv)
src/Transform.cpp         Tag-based registration: LSQ6/7/9/10/12 + TPS (Eigen)
src/QCState.cpp           QC CSV parsing (RFC 4180) + verdict state
src/Prefetcher.cpp        QC row prefetching (main thread only)
src/BackendFactory.cpp    Runtime backend selection + fallback chain
src/WindowManager.cpp     GLFW window (non-macOS); src/WaylandTouchInput.cpp — Wayland touch gestures
src/VulkanBackend.cpp, VulkanHelpers.cpp, OpenGL2Backend.cpp    GPU backends
src/headless_test_main.cpp, src/backends/imgui_impl_osmesa.*    Headless (OSMesa) UI test harness
src/mincpik/              new_mincpik — headless mosaic PNG generator
src/qc/                   new_qc — standalone image QC viewer (namespace QC::, own backends); CSVHandler is the qc_csv library, shared with mriv_qc
include/                  Headers mirroring src/
third_party/niftilib/     Vendored NIfTI reader, compiled into nr_core
tests/                    CTest sources, reference PNGs, sq1/sq2 test volumes, debug tools
```

---

## Architecture invariants

- **`nr_core` stays GPU-free** (Volume, NiftiVolume, VolumeReorient, Transform, AppConfig, SliceRenderer, ColourMap, TagWrapper, CliArgs) — `new_mincpik` and `mriv` link it headless.
- **`Interface` talks only to the abstract `GraphicsBackend`** (`createTexture`, `updateTexture`, `beginFrame`, `endFrame`, `captureScreenshot`, `requestClose`, `windowSize`, …), never to GLFW directly.
- **Backend fallback chain:** Vulkan → OpenGL2 → OpenGL2-EGL (`BackendFactory.cpp` orders backends; the EGL retry lives in `main.cpp`; `--backend` overrides). Vulkan texture uploads wait on a `VkFence`, not `vkQueueWaitIdle`; `VulkanHelpers` keeps a persistent staging buffer and a dedicated upload command pool.
- **QC verdict state lives in `QCState`**, not `AppState`; `Interface` polls it.
- **Dock layout** is rebuilt from scratch in `Interface::render()` whenever the viewport size changes; all splits are fractional.
- `mriv` uses cxxopts while the parent hand-rolls CLI parsing — a deliberate choice; don't "align" them.

---

## Coding standards (C++)

| Aspect | Rule |
|--------|------|
| Classes | `PascalCase` |
| Methods / variables | `camelCase` |
| Namespace | `QC::` for all code in `src/qc/` |
| Error output | `std::cerr` — never `printf` to stderr |
| Braces | Allman style (opening brace on its own line) |
| Indentation | 4 spaces, no tabs |
| Includes | system headers → library headers → local headers |

---

## Known open issues

**None currently.** All 76 CTest suites pass on Linux. Record new issues in [`problem.md`](problem.md)
and link them here. `problem.md` also keeps the resolved OverlapTest post-mortem — the reference for
the `voxelToWorld` / `worldToVoxel` conventions.
