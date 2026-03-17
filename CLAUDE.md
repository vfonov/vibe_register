# CLAUDE.md — Project Master Index

This file is the top-level reference for AI agents working in this repository.
Read this first, then consult the linked files for deeper detail.

---

## Repository Overview

This repository contains:

| Directory | Description |
|-----------|-------------|
| `legacy/register/` | Original BIC `register` C tool — **read-only reference, do not modify** |
| `legacy/bicgl/` | Original BIC Graphics Library — **read-only reference, do not modify** |
| `new_register/` | Modern C++17 rewrite of `register` (Vulkan + ImGui + MINC2) — includes `new_qc` viewer |

---

## Key Documentation Files

| File | What it covers |
|------|----------------|
| [`AGENTS.md`](AGENTS.md) | **Agent operating instructions** — build commands, code style, workflow rules |
| [`research.md`](research.md) | **Deep codebase review** — architecture diagrams, module analysis, test coverage, risks, and recommendations |
| [`new_register/README.md`](new_register/README.md) | User-facing usage documentation for new_register and new_qc |
| [`PLAN.md`](PLAN.md) | Feature roadmap and remaining work items |

---

## new_register — Source Files

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
└── tests/                      14 unit test suites (CMake/CTest)
```

**Build:**
```bash
cd new_register/build && cmake .. && make
ctest --output-on-failure
```

**Key architectural notes:**
- `nr_core` static library bundles GPU-agnostic code (Volume, Transform, AppConfig, SliceRenderer, etc.) for reuse by `new_mincpik`.
- QC mode: `--qc input.csv --qc-output results.csv`; input columns are volume paths; verdicts written per-column.
- Backend fallback chain: Vulkan → OpenGL2 → OpenGL2-EGL.
- Prefetcher runs on the **main thread only** (libminc/HDF5 not thread-safe).
- `Interface.cpp` (~2273 lines) is the largest file and a refactoring candidate.

---

## Legacy Code (Reference Only)

```
legacy/
├── register/       Original C register application — DO NOT MODIFY
│   ├── User_interface/     UI components
│   └── Functionality/      Core slicing, tagging, volume logic
└── bicgl/          BIC Graphics Library — DO NOT MODIFY
    ├── OpenGL_graphics/
    └── GLUT_windows/
```

---

## Coding Standards Summary

| Project | Standard | Style | Naming |
|---------|----------|-------|--------|
| `new_register` (incl. `src/qc/`) | C++17 | PascalCase classes, camelCase methods, `QC::` namespace for qc/ | `std::cerr` for errors |
| `legacy/` | C (read-only) | Allman braces, 4-space indent, snake_case | `fprintf(stderr)` |

**Never touch `legacy/`.**
When modifying hotkeys in `new_register`, always update `Interface::renderHotkeyPanel()`.
