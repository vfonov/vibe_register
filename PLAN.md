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
