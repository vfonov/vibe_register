# PLAN.md — new_register Roadmap

Open work only. Implemented features are described in
[`new_register/README.md`](new_register/README.md); history is in `git log`.

---

## Not Yet Implemented

### Tag Points
- [ ] Edit tag labels in the tag table
- [ ] Tag marker colours: inside/outside the current slice, active/inactive (markers are drawn, in a single colour)
- [ ] Up/Down arrow keys to navigate between tags

### Resampling
- [ ] Resample volume 2 into volume 1's space using the computed transform
- [ ] Progress indicator during resampling

### Slice Filtering
- [ ] Per-view filter type: Nearest Neighbour, Linear, Box, Triangle, Gaussian
- [ ] Configurable FWHM

### Interpolation
- [ ] Interpolation methods: Nearest Neighbor, Linear, Cubic
- [ ] Per-volume interpolation type selection

### Additional Volume Support
- [ ] RGB/vector volume support
- [ ] 4D volumes with time dimension

### UI / Analysis
- [ ] Undo/redo for display adjustments
- [ ] Histogram / intensity statistics
- [ ] Measurement tools (distance, angle, area)

---

## Test Coverage Gaps

- [ ] Transform: LSQ10 has no dedicated test (LSQ6/7/9/12/TPS do)
- [ ] TagWrapper: `removeTag()` (index shifting, `points2_` sync), `updateTag()`, `clear()`
- [ ] AppState: `VolumeCache` LRU get/put/eviction, `getTagPairs()`, `applyConfig()`, `initializeViewStates()`, `saveTags()` routing, `loadCombinedTags()` / `saveCombinedTags()`
- [ ] BackendFactory: `backendName()`, `parseBackendName()` including aliases
- [ ] ColourMap: `colourMapRepresentative()`

---

## Catch2 Test Migration

All C++ unit tests move to **Catch2 v3**, resolved in `new_register/CMakeLists.txt` under
`ENABLE_TESTS` just before `add_subdirectory(tests)` (system package preferred, else FetchContent at
a pinned tag, currently `v3.16.0`). Motivation: the current suites mix three assertion styles
(plain `assert`, hand-rolled `CHECK` + failure counter, `TEST`/`PASS`/`FAIL` macros), and plain
`assert` compiles to nothing under `-DNDEBUG`, so those suites pass without checking anything in a
Release build.

**Conventions**

- `add_nr_test(<name> CATCH2 ...)` / `add_mriv_test(<name> CATCH2 ...)` links `nr_test_main`, which
  supplies `main()` (a `Catch::Session`) plus `--data-dir` / `--src-dir`, read through
  `tests/nr_test.h` (`nrtest::dataDir()`, `nrtest::srcDir()`). A missing fixture must fail
  (`REQUIRE`), never skip.
- One CTest entry per executable, existing CTest names kept (`ctest -R OverlapTest`, `-R "^mriv_"`
  and the suite count keep working). `catch_discover_tests()` can replace this later in the macro.
- Conversion: each test function → `TEST_CASE("...", "[module]")`, shared setup → `SECTION`;
  `assert`/`CHECK` → `REQUIRE` where later code depends on it (indexing, dereferencing), `CHECK`
  otherwise; Catch2 rejects `a && b` inside an assertion, so split those; `approxEqual`-style helpers
  → `Catch::Matchers::WithinAbs` / `WithinRel`. Delete the old macros, counters and `main()`.
- Per converted file: CTest name unchanged, reported assertion count comparable to the old checks,
  and one deliberately broken expectation makes it fail.
- Out of scope: `headless_ui_tests` (imgui_test_engine harness) and the debug tools (`dump_vol`,
  `generate_*`).

**Batches**

- [x] Phase 1–2: Catch2 lookup/fetch, `nr_test_main` + `nr_test.h`, `CATCH2` keyword in `add_nr_test()`
- [x] 3a Pilot: `test_colour_map`, mriv `test_layout`
- [ ] 3b Remaining 21 mriv suites (plain `assert` — mostly mechanical)
- [ ] 3c Parent tests without data: transform, app_config, qc_csv, overlay_blend, volume_core,
      volume_reorient, label_rounding, robust_nonfinite, `csv_test` (new_qc; `nr_test_main` does
      not depend on `nr_core`)
- [ ] 3d Parent data-driven tests: real_data, coordinate_sync, world_to_voxel, center_of_mass,
      thick_slices_com, matrix_debug, tag_load, volume_info, minc_dims, nifti_*, minc_reorient_match,
      overlap/overlap2, coord_sq2tr, mincpik, test_mode — move paths to `--data-dir` / `--src-dir`
- [ ] 3e Headless OSMesa tests (`test_coordinate_input`, `test_column_layout`) — GL/ImGui context
      setup into a fixture
- [ ] 4 Cleanup: make Catch2 the `add_nr_test()` default and drop the `CATCH2` keyword; make
      `add_nr_test()` honour `ARGS` (and drop the workaround in `add_mriv_test()`); remove the GCC < 10
      `stdc++fs` loop if no longer needed
- [ ] 5 Docs: drop the "unmigrated suites" notes from `AGENTS.md`, `mriv/CLAUDE.md`,
      `mriv/PLAN.md`, `mriv/PLAN_TESTING.md`, `mriv/HANDOFF.md`

---

## new_mincpik Performance TODO

CPU hot spots in the headless mosaic pipeline, roughly by impact.

- [ ] **Sagittal extraction** (`SliceRenderer.cpp`, sagittal branch of slice extraction): reads `vdata[zOff + y * dimX]` with a stride of `dimX` per pixel. Gather the column into a contiguous buffer first.
- [ ] **Overlay compositing** (`renderOverlaySlice`): does a per-pixel `refV2W * dvec4` plus a per-volume `worldToVox` dmat4 multiply. Step incrementally along px/py in target-voxel space for linear transforms (TPS must keep the per-pixel path).
- [ ] **Log transform**: `std::log10()` per pixel when `useLogTransform` is set. Precompute a LUT over `[rangeMin, rangeMax]`.
- [ ] **`resampleToPhysicalAspect`** (`mosaic.cpp`): per-pixel bounds checks in the inner loop.
- [ ] **Per-slice allocations** in `mincpik_main.cpp` (three buffers per slice): reuse them.
- [ ] **`computeQuantiles()`**: still copies the finite voxels once per call. Use `nth_element` on a reused buffer, or a histogram.
- [ ] **Final scale** in `mincpik_main.cpp`: division per pixel. Use a reciprocal multiply.
- [ ] **LUT inversion**: with `invertColourMap` set, the inverted LUT is rebuilt on every render. Cache it.
- [ ] **`applyCrop`** (`mosaic.cpp`): allocates before the range check.

---

## WebAssembly (Browser) Build

Build new_register to run in a web browser using WebAssembly.

### Required Software

| Software | Version | Purpose |
|----------|---------|---------|
| **Emscripten** | ≥3.1.0 | C++ → WebAssembly compiler |
| **Node.js** | ≥18 | Running WASM in test server |
| **Python** | ≥3.6 | Emscripten build scripts |

**Installation:**
```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install 3.1.64
./emsdk activate 3.1.64
source ./emsdk_env.sh
emcc --version
```

### CMake Configuration

Add to `CMakeLists.txt`:
```cmake
option(ENABLE_WEBASSEMBLY "Build for WebAssembly (browser)" OFF)

if(ENABLE_WEBASSEMBLY)
    # Use WebGL2 (OpenGL ES 3.0)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sUSE_WEBGL2")
    
    # Use GLFW (works with Emscripten via -sUSE_GLFW=3)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sUSE_GLFW=3")
    
    # Preload local files for browser access
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --preload-file test_data")
    
    # Output as HTML
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -o index.html")
    
    # Disable native backends (not available in browser)
    set(ENABLE_VULKAN OFF)
    set(ENABLE_OPENGL2 OFF)
endif()
```

### Files to Modify

| File | Changes |
|------|---------|
| `CMakeLists.txt` | Add `ENABLE_WEBASSEMBLY` option, conditional linker flags |
| `src/main.cpp` | Add Emscripten canvas resize callback, conditionally disable native backends |
| `src/Volume.cpp` | Use fetch + memory filesystem for file loading |

### Implementation Details

1. **Graphics Backend**: Use ImGui's OpenGL2 backend (works with WebGL2 in browser via `-sUSE_WEBGL2`)
2. **Window Management**: GLFW works with Emscripten; use `ImGui_ImplGlfw_InstallEmscriptenCallbacks()` for canvas resize
3. **File I/O**: Use `--preload-file` to bundle MINC files, or fetch via JavaScript and load into Emscripten's MEMFS

### Build Commands

```bash
# Standard native build
cd new_register/build
cmake -DENABLE_VULKAN=ON -DENABLE_OPENGL2=ON ..
make

# WebAssembly build
cd new_register/build
emcmake cmake -DENABLE_WEBASSEMBLY=ON ..
emmake make
# Output: index.html, index.js, index.wasm
```

### Serving Locally

```bash
# Using Python
python -m http.server 8080

# Or using Node.js
npx serve .
# Then open http://localhost:8080/index.html
```

### Complexity Estimate

| Task | Complexity |
|------|------------|
| CMake flag | Easy |
| WebGL2 via Emscripten | Easy |
| Emscripten canvas callbacks | Easy |
| File preloading | Medium |

**Full implementation:** 1-2 weeks  
**Minimum viable:** 2-3 days (basic rendering, limited file loading)


---

## C++23 Modernization (Future)

The codebase targets C++17. A future upgrade to C++23 is planned. Phases 1–2 (unique_ptr, std::array) are complete. Phases 3, 4 and 7 need C++23; Phases 5 and 6 are plain C++17 cleanups that can be done any time:

- **Phase 3:** `std::views::iota`, `std::ranges::minmax_element` in ColourMap.cpp / Volume.cpp
- **Phase 4:** `constexpr` / `consteval` for LUT helpers; replace `countOf` template with `std::size()`
- **Phase 5:** Structured bindings in AppConfig.cpp, TagWrapper.cpp, main.cpp
- **Phase 6:** Replace magic numbers (hardcoded `256`, `sizeof`/`sizeof` ratios)
- **Phase 7:** `std::format` / `std::print` replacing `printf`/`fprintf`/`std::cout`
