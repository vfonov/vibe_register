# PLAN.md — new_register Rewrite Status

## Overview

Modern C++17 rewrite of the legacy `register` application using Vulkan/OpenGL 2, ImGui (Docking), GLFW, and nlohmann/json.

---

## Completed Features

### Volume Management
- [x] MINC2 volume loading via `minc2-simple`
- [x] Multi-volume support (CLI and config)
- [x] Synthetic test data fallback (`--test` flag)
- [x] Per-volume min/max computation
- [x] World-space extent and pixel aspect ratio calculations

### Slice Viewing
- [x] 3-plane slice views per volume (transverse, sagittal, coronal)
- [x] Correct aspect ratio accounting for non-uniform voxel spacing
- [x] Slice navigation via mouse (left-click, middle-drag, scroll wheel) and `+`/`-` keyboard shortcuts
- [x] Yellow crosshair overlay showing other views' slice positions
- [x] Zoom-aware slice expansion: zoomed views expand to fill available panel space

### Mouse Interaction
- [x] Left click/drag: set cross-slice positions
- [x] Middle drag: scroll through slices
- [x] Shift+Left drag: pan
- [x] Shift+Middle drag: zoom
- [x] Mouse wheel: zoom centered on cursor

### Multi-Volume Synchronization
- [x] Separate sync controls: Sync Cursor, Sync Zoom, Sync Pan checkboxes
- [x] Symmetric sync: works both ways (volume-to-overlay and overlay-to-volume)
- [x] Voxel-to-world and world-to-voxel matrices built correctly in `Volume::load()`
- [x] World (0,0,0) maps to correct voxel for both test volumes

### Colour Maps
- [x] 21 colour map types (Gray, HotMetal, Spectral, Red, Green, Blue, negative variants, Contour, etc.)
- [x] Quick-access swatch buttons with gradient preview
- [x] "More..." dropdown for all 21 maps
- [x] Per-volume colour map selection
- [x] Configurable under/over range colours (Current, Transparent, or any colour map)
- [x] `colourMapByName()` — valid name lookup, invalid name returns nullopt

### Overlay / Merged View
- [x] Alpha-blended composite of all volumes (when >1 volume loaded)
- [x] World-coordinate resampling (nearest-neighbour)
- [x] Per-volume alpha sliders (3+ volumes), single blend slider for two-volume case
- [x] Own zoom/pan/crosshair controls
- [x] Overlay visibility toggle in Tools panel, persisted in config

### Configuration
- [x] JSON config persistence using nlohmann/json
- [x] Config loaded from `./config.json` or `--config` path
- [x] Per-volume paths, colour maps, value ranges, slice indices, zoom, pan
- [x] Tools panel with Save Config, Load Config, Reset All Views, Quit buttons
- [x] Save Config and Load Config use file chooser dialog
- [x] QC mode: Save Config stores QC column configs (colour map, value range)
- [x] QC mode: Load Config applies QC column configs and reloads current row
- [x] QC mode: Colour map and value range changes auto-saved to column configs

### Command Line Options
- [x] `-c`, `--config <path>` — load config from a specific path
- [x] `-h`, `--help`
- [x] `--test` — launch with a generated test volume (no-input prints help and exits with code 1)
- [x] Positional volume file arguments
- [x] `-r`/`--red`, `-g`/`--green`, `-b`/`--blue`, `-G`/`--gray`, `-H`/`--hot`, `-S`/`--spectral`
- [x] `--lut <name>` — set any colour map by name for next volume
- [x] `--range <min>,<max>` — set value range for next volume (e.g., `--range 0,100`)
- [x] `-B`/`--backend <name>` — graphics backend: `auto`, `vulkan`, `opengl2` (default: auto)
- [x] `--sync` — synchronize all (cursor, zoom, pan)
- [x] `--sync-cursor` — synchronize cursor position across volumes
- [x] `--sync-zoom` — synchronize zoom level across volumes
- [x] `--sync-pan` — synchronize pan position across volumes
- Example: `new_register --gray vol1.mnc -r vol2.mnc`

### Multi-Backend Graphics
- [x] **Vulkan** — Full-featured backend (swapchain, command buffers, staging buffers)
- [x] **OpenGL 2** — Fixed-function pipeline backend for legacy Linux, software renderers, SSH/X11
- [x] Runtime auto-detection prefers Vulkan > OpenGL2, with CLI override (`--backend`)
- [x] Automatic fallback: OpenGL2/GLX -> OpenGL2/EGL -> Vulkan -> error
- [x] EGL fallback for X2Go/nxagent (GLX 1.2 only environments)
- [x] Abstract `GraphicsBackend` interface with `Texture` opaque handle
- [x] CMake options: `ENABLE_VULKAN`, `ENABLE_OPENGL2`, `ENABLE_METAL`
- [x] ImGui docking (multi-viewport removed for SSH/X11 compatibility)
- [x] HiDPI support (GLFW content scale, ImGui style/font scaling)
- [x] Proportional column resizing on window resize
- [x] Draggable view height splitters synchronized across all columns (including overlay panel)

### Keyboard Shortcuts
- [x] `R` — Reset All Views
- [x] `Q` — Quit
- [x] `C` — Toggle clean mode
- [x] `P` — Screenshot (save window as PNG)
- [x] `+`/`-` — Step through axial (Z) slice

### Tag Points
- [x] Load/save `.tag` files via TagWrapper class
- [x] Tag list window with table (index, label, coordinates per volume)
- [x] Tag selection updates cursor position in all volumes
- [x] Right-click on slice to create new tag
- [x] Delete selected tag button
- [x] Tags window always visible (unless in QC or Clean mode), docked below Tools panel
- [x] **Tag Editing**: Selected tag shown with larger circle (8 voxels vs 5); Right-click in Edit mode moves selected tag to cursor position; Tag Mode toggle (Add/Edit)
- [x] **Load/Save**: Load Tags and Save Tags buttons with ImGui file dialog for .tag file selection
- [x] Load tags from command line (`--tags`/`-t` CLI argument, combined two-volume .tag support)
- [x] **Auto-save Tags**: Checkbox in Tags panel to enable/disable auto-save on tag changes

### Label Volume Support
- [x] **Label flag**: CLI options `--label` / `-l` to mark volume as label/segmentation
- [x] **Label description file**: CLI options `--labels` / `-L` to specify label colour LUT file
- [x] **Label LUT format**: Tab or space-separated file with columns: label_id, R, G, B, A, visibility, mesh, name
- [x] **Parser**: Handles both tabs and spaces, multiple spaces between values, comment lines (#)
- [x] **Internal storage**: Voxels remain as doubles (no change to internal representation)
- [x] **Rendering**: Use label colour LUT instead of colour map when label volume flag is set
- [x] **Overlay rendering**: Label colours also used in overlay composite view
- [x] **UI panel**: "Label Volume" checkbox in volume controls; displays label name at cursor position
- [x] **Default LUT**: If no label description file, generates grayscale based on label ID

### Screenshot
- [x] Capture entire window to PNG via `P` key or button
- [x] Auto-incrementing filenames, never overwrites
- [x] Works with both Vulkan and OpenGL 2 backends

### Tests
- [x] 14 test suites (volume loading, colour maps, transforms, coordinate sync, tags, QC CSV, AppConfig, etc.)

### Transform Computation
- [x] Compute transform from tag point pairs (volumes 0 and 1)
- [x] Transform types: LSQ6 (rigid), LSQ7 (similarity), LSQ9, LSQ10, LSQ12 (full affine), TPS (thin-plate spline)
- [x] Eigen Levenberg-Marquardt optimizer for LSQ6/7/9/10 (replaced broken Nelder-Mead)
- [x] Direct linear solve for LSQ12, thin-plate spline for TPS
- [x] `R * S` affine parameterization (rotate-after-scale) for correct convergence
- [x] SVD Procrustes initial guess for LSQ6/7; LSQ12 polar decomposition for LSQ9/10
- [x] Average and per-tag RMS error display in Tags panel
- [x] Per-tag RMS shown as column in tag table
- [x] Transform auto-recomputed when tags change (dirty flag)
- [x] Dropdown (Combo) for transform type selection
- [x] Save `.xfm` files via minc2-simple API (linear) and hand-written (TPS)
- [x] Read `.xfm` files via minc2-simple API
- [x] User-editable `.xfm` output filename (InputText)
- [x] Save `.tag` button alongside Save `.xfm`
- [x] 11 transform subtests passing (identity, translation, rotation, scaling, mixed, all LSQ types, TPS, XFM I/O)

### Overlay Transform Visualization
- [x] When a valid tag-based transform exists, vol1 overlay is resampled through the computed registration
- [x] Linear transforms: inverse matrix inserted into combined scanline transform (zero per-pixel overhead)
- [x] TPS transforms: per-pixel Newton-Raphson inversion via `inverseTransformPoint()` on `TransformResult`
- [x] Falls back to original (identity) overlay compositing when no transform is available
- [x] Transform recomputed immediately when tags are created or deleted (timing fix)

### Bug Fixes
- [x] Sagittal view tag placement: `drawTagsOnSlice()` had `dimU`/`dimV` swapped for viewIndex==1 (was U=Z,V=Y; corrected to U=Y,V=Z)
- [x] Overlay transform timing: moved `recomputeTransform()` to the top of the render loop so the transform is always up-to-date before overlay textures are built; also rebuilds overlays after transform type radio button changes and tag deletion
- [x] QC cursor preservation: restored slice indices when switching QC rows (previously only zoom/pan were preserved)
- [x] Config save: fixed saving of crosshairs visibility and tag list window state to config file
- [x] Error handling: added `GL_CHECK` macro with `checkGLError()` for all OpenGL calls; added Vulkan validation layer callback and VkResult checks for command buffer operations; added GLFW error callback

---

## Test Coverage Gaps

Audit of all unit-testable public functions across source modules (excluding pure UI/GPU code).
14 test suites currently pass. Approximate overall coverage of unit-testable functions: **~30%**.

### Quantitative Summary

| Module | Public Functions | Tested | Not Tested | Coverage |
|---|---|---|---|---|
| **Volume** | 18 | 8 | 10 | 44% |
| **TagWrapper** | 18 | 10 | 8 | 56% |
| **Transform** | 6 | 5 | 1 | 83% |
| **ColourMap** | 5 | 3 | 2 | 60% |
| **AppConfig** | 10 | 10 | 0 | **100%** |
| **AppState** | 25 | 0 | 25 | **0%** |
| **QCState** | 8 | 7 | 1 | 88% |

### HIGH Priority

Core logic, easily testable without GPU, high impact if broken.

#### AppConfig (100% — fully tested)
- [x] `loadConfig()` — missing file returns default, valid JSON round-trip, malformed JSON throws
- [x] `saveConfig()` — write + re-read round-trip
- [x] `to_json` / `from_json` for `VolumeConfig`, `GlobalConfig`, `QCColumnConfig`, `AppConfig` — optional fields, all fields populated vs minimal

#### AppState::VolumeCache (0% — completely untested)
- [ ] `get()` / `put()` — cache hit, cache miss, LRU eviction order
- [ ] `clear()` — empties cache, `size()` returns 0
- [ ] Thread safety — concurrent `get`/`put` from multiple threads

#### AppState (0% — completely untested)
- [ ] `getTagPairs()` — extraction of paired tags from volumes 0 and 1
- [ ] `applyConfig()` — config → view state mapping (colour maps, value ranges, slice indices, zoom, pan)
- [ ] `initializeViewStates()` — midpoint slice index calculation, value range from volume min/max
- [ ] `saveTags()` — routing logic: combined path set → two-volume save; empty → per-volume save
- [ ] `loadCombinedTags()` — two-volume .tag file distribution to volumes 0 and 1
- [ ] `saveCombinedTags()` — assemble two-volume tag data from volumes 0 and 1

#### Transform
- [x] `inverseTransformPoint()` — linear path (glm::inverse), TPS path (Newton-Raphson convergence)
- [ ] LSQ10 — no dedicated test (LSQ6/7/9/12/TPS all have specific tests)

#### TagWrapper
- [ ] `removeTag()` — index shifting, `points2_` synchronization for two-volume data
- [ ] `updateTag()` — position + label update at given index
- [ ] `clear()` — resets points, points2, labels, volume count

### MEDIUM Priority

Utility functions, static helpers, secondary paths.

#### Volume
- [ ] `get()` — bounds-checked voxel access, out-of-bounds returns 0
- [ ] `worldExtent()` — physical extent computation from step × dimensions
- [ ] `slicePixelAspect()` — pixel aspect ratio for non-isotropic voxels
- [ ] `generate_test_data()` — produces valid dimensions, data, and matrices
- [ ] `clearTags()` — resets tag state

#### ColourMap
- [ ] `colourMapRepresentative()` — returns valid RGBA floats in [0,1] for all map types

#### BackendFactory (static utilities only)
- [ ] `backendName()` — enum-to-string for all backend types
- [ ] `parseBackendName()` — string-to-enum including aliases ("vk", "gl2", "gl", "mtl", "auto")

#### AppState (secondary)
- [ ] `getMaxTagCount()` — max tag count across all loaded volumes
- [ ] `anyVolumeHasTags()` — true if any volume has tags
- [ ] `setSelectedTag()` — updates cursor position in all volumes from selected tag
- [ ] `recomputeTransform()` — dirty-flag gating, integration with `computeTransform()`

### LOW Priority

Copy/move semantics, threading, trivial accessors.

- [ ] **Volume** — copy/move constructors and assignment operators
- [ ] **TagWrapper** — copy/move constructors and assignment operators
- [ ] **Prefetcher** — `requestPrefetch()`, `cancelPending()`, `shutdown()` (integration test)
- [ ] **AppState** — `clearAllVolumes()`, `loadVolumeSet()` (needs MINC test data)
- [ ] **QCState** — `pathsForRow()` (trivial accessor)

---

## Not Yet Implemented

### Tag Points (remaining)
- [ ] Edit tag labels in table
- [ ] Tag markers displayed on slices (inside/outside colours, active/inactive colours)
- [ ] Up/Down arrow keys to navigate between tags

### Resampling
- [ ] Resample volume 2 into volume 1's space using computed transform
- [ ] Progress indicator during resampling

### Slice Filtering
- [ ] Per-view filter type: Nearest Neighbour, Linear, Box, Triangle, Gaussian
- [ ] Configurable FWHM

### Interpolation
- [ ] Add interpolation methods: Nearest Neighbor, Linear, Cubic
- [ ] Per-volume interpolation type selection

### Additional Volume Support
- [ ] Support up to 8 volumes (legacy supports `N_VOLUMES = 8`)
- [x] NIfTI-1 format support (loader exists, coordinate bug — see below)
- [ ] RGB/vector volume support
- [ ] 4D volumes with time dimension

---

## NIfTI Loader Coordinate Fix

The NIfTI loader (`NiftiVolume.cpp`) can load and display NIfTI files, but
produces incorrect world coordinates compared to the equivalent MINC file.
The root cause is that the loader skips the spatial axis permutation and
decomposition that `nii2mnc` performs.  See `research.md` §11 for the full
mathematical analysis.

### Current state

- `NiftiMncMatchTest` loads the same volume as `.nii.gz` and `.mnc` and
  compares rendered central slices.  Currently fails: 31k–40k pixel
  mismatches per view, 388 mm corner distance.
- All 20 other tests pass.

### Fix plan

The fix replaces lines 89–149 of `NiftiVolume.cpp` with an implementation
that replicates the `nii2mnc` algorithm exactly.  The algorithm has three
stages (see `research.md` §11.3 for derivation):

#### Step 1 — Read the affine

Read `sform` (preferred) or `qform` from the `nifti_image`.  This is already
correct in the current code (lines 72–87).  No changes needed.

#### Step 2 — Spatial axis permutation

For each file axis `j ∈ {0,1,2}`, determine which MINC spatial dimension it
most closely aligns with by finding the largest absolute component in column
`j` of the affine:

```cpp
int spatial_axes[3];
for (int j = 0; j < 3; ++j)
{
    float cx = fabsf(nii_xfm.m[0][j]);
    float cy = fabsf(nii_xfm.m[1][j]);
    float cz = fabsf(nii_xfm.m[2][j]);
    if (cy > cx && cy > cz)      spatial_axes[j] = 1;
    else if (cz > cx && cz > cy) spatial_axes[j] = 2;
    else                          spatial_axes[j] = 0;
}
```

#### Step 3 — Permute columns into MINC order

Create a permuted matrix where column `d` corresponds to MINC spatial axis `d`:

```cpp
double perm[3][3], origin[3];
for (int j = 0; j < 3; ++j)
    for (int row = 0; row < 3; ++row)
        perm[row][spatial_axes[j]] = nii_xfm.m[row][j];
for (int row = 0; row < 3; ++row)
    origin[row] = nii_xfm.m[row][3];
```

#### Step 4 — Decompose into dirCos, step, start

For each spatial dimension `d`:

```
axes[d]   = column d of perm            (3-vector)
mag       = ‖axes[d]‖
sign      = (axes[d][d] < 0) ? −1 : +1
step[d]   = sign × mag × unit_scale
dirCos[d] = axes[d] / (sign × mag)      (unit vector, positive diagonal)
```

Then solve the 3×3 linear system `C · start = origin × unit_scale` where
`C = [dirCos[0] | dirCos[1] | dirCos[2]]` using `glm::inverse(C) * origin`.

#### Step 5 — Build voxelToWorld from MINC components

Reconstruct the 4×4 matrix using the same formula as `Volume.cpp` lines
247–267 (the MINC loader):

```
M₃ₓ₃ = C · diag(step)
T     = C · start
voxelToWorld = [M₃ₓ₃ | T; 0 0 0 1]
worldToVoxel = inverse(voxelToWorld)
```

This ensures `voxelToWorld` is consistent with the stored dirCos/step/start.

#### Step 6 — Permute voxel data to match MINC axis order

After the spatial axis permutation, the MINC dimension order may differ from
the NIfTI file order.  The voxel data array must be transposed so that the
fastest-varying axis in memory corresponds to xspace (axis 0), the next to
yspace (axis 1), and the slowest to zspace (axis 2).

If `spatial_axes = {0, 1, 2}` (identity — file axes already match MINC
order), no transpose is needed.  Otherwise, transpose the 3D data array
according to the permutation.  Also permute `vol.dimensions` accordingly.

### Verification

1. `NiftiMncMatchTest` must pass (0 pixel mismatches on all three views).
2. All 20 existing tests must continue to pass.
3. `NiftiLoadTest` (existing) must continue to pass.
4. Manual check: load the `.nii.gz` and `.mnc` side-by-side in
   `new_register` — slices must look identical with synced cursors.

### Files to modify

| File | Changes |
|------|---------|
| `src/NiftiVolume.cpp` | Replace lines 89–149 with the 6-step algorithm above |

No other files need changes.  The test and CMakeLists.txt registration
already exist.

---

## QC (Quality Control) Mode

A special mode for batch quality control of medical imaging datasets. Tags are
completely disabled. The user reviews datasets from a CSV file, rating each
volume column PASS/FAIL with optional comments.

### Example Usage
```bash
new_register --qc subjects.csv --qc-output results.csv --config qc_config.json
```

### Input CSV Format
```csv
ID,T1,T2
sub01,/data/sub01_t1.mnc,/data/sub01_t2.mnc
sub02,/data/sub02_t1.mnc,/data/sub02_t2.mnc
```
- First column must be `ID`. Remaining columns are volume file paths.

### Output CSV Format (auto-saved on every verdict/comment change)
```csv
ID,T1_verdict,T1_comment,T2_verdict,T2_comment
sub01,PASS,,FAIL,motion artifact
sub02,,,,
```

### Config JSON
```json
{
  "global": {
    "syncCursors": true,
    "showOverlay": true
  },
  "qc_columns": {
    "T1": { "colourMap": "GrayScale", "valueMin": 0, "valueMax": 100 },
    "T2": { "colourMap": "HotMetal", "valueMin": 0, "valueMax": 200 }
  }
}
```

### QC Keyboard Shortcuts
| Key | Action |
|---|---|
| `]` | Next dataset |
| `[` | Previous dataset |
| `+`/`-` | Step axial slice |
| `Q` | Quit |
| `R` | Reset views |
| `C` | Clean mode |
| `P` | Screenshot |

### QC Features
- [x] Per-column PASS/FAIL verdicts with comments
- [x] QC dataset list embedded in Tools panel with per-column status indicators
- [x] Auto-save on verdict/comment change, manual save button
- [x] Preserves display settings (colour map, value range, zoom, pan, slice indices) across row switches
- [x] Missing file handling with placeholder panels
- [x] First-unrated-row auto-jump on startup
- [x] Resizable columns in QC list with horizontal scroll

---

## Performance Optimizations

### Tier 1: Quick Wins
- [x] Guard `syncCursors()` with dirty flag (skip when cursors unchanged)
- [x] Remove texture rebuilds from `syncZoom()`/`syncPan()` (zoom/pan don't change slice data)
- [x] Deduplicate overlay texture rebuilds (once per frame, not per column)
- [x] CSV save on field deactivation (not every keystroke)

### Tier 2: Rendering Optimizations
- [x] **Persistent Vulkan staging buffer** — Single persistent buffer/memory/command buffer, grows on demand, never shrinks. Eliminates per-call Vulkan allocations.
- [x] **Overlay compositing** — Precomputed combined ref-to-target transform, affine scanline increments, hoisted LUT pointers, pre-resolved clamp colours, reciprocal multiply, direct data pointer with `getUnchecked()`.
- [x] **Pixel buffer reuse** — Persistent `pixelBuf_` member in ViewManager, resized but never shrinks.
- [x] **LRU volume cache** — `VolumeCache` class (8 entries default) avoids re-reading MINC files during QC row switches. Cache-aware `loadVolumeSet()` checks cache first.

### Tier 3: Background Prefetch
- [x] **Thread-safe `VolumeCache`** — All methods acquire `std::mutex` for concurrent access.
- [x] **`Prefetcher` class** — Background `std::thread` loads volumes for adjacent QC rows into cache. Uses condition variable for notification, atomic flags for cooperative cancellation.
- [x] **Adjacent row prefetch** — After each row switch, prefetcher loads prev + next row volumes. Skips already-cached paths.
- [x] **Initial prefetch on startup** — Kicks off immediately after loading first QC row.

---

## Architecture

### File Structure
```
new_register/
├── CMakeLists.txt
├── include/
│   ├── AppConfig.h
│   ├── AppState.h
│   ├── ColourMap.h
│   ├── GraphicsBackend.h
│   ├── Interface.h
│   ├── OpenGL2Backend.h
│   ├── Prefetcher.h
│   ├── QCState.h
│   ├── SliceRenderer.h
│   ├── TagWrapper.hpp
│   ├── Transform.h
│   ├── Volume.h
│   ├── ViewManager.h
│   ├── VulkanBackend.h
│   └── VulkanHelpers.h
├── src/
│   ├── main.cpp
│   ├── AppState.cpp
│   ├── AppConfig.cpp
│   ├── BackendFactory.cpp
│   ├── ColourMap.cpp
│   ├── Interface.cpp
│   ├── OpenGL2Backend.cpp
│   ├── Prefetcher.cpp
│   ├── QCState.cpp
│   ├── SliceRenderer.cpp
│   ├── TagWrapper.cpp
│   ├── Transform.cpp
│   ├── ViewManager.cpp
│   ├── Volume.cpp
│   ├── VulkanBackend.cpp
│   ├── VulkanHelpers.cpp
│   └── mincpik/
│       └── mincpik_main.cpp
└── tests/
    ├── test_real_data.cpp
    ├── test_colour_map.cpp
    ├── test_world_to_voxel.cpp
    ├── test_coordinate_sync.cpp
    ├── test_matrix_debug.cpp
    ├── test_minc_dims.cpp
    ├── test_volume_info.cpp
    ├── test_tag_load.cpp
    ├── test_thick_slices_com.cpp
    ├── test_center_of_mass.cpp
    ├── test_qc_csv.cpp
    ├── test_transform.cpp
    ├── test_app_config.cpp
    └── test_mincpik.cpp
```

### Component Design
- **AppState**: Data layer (volumes, view states, LRU cache, config)
- **ViewManager**: Logic layer (texture generation, cursor sync, overlay compositing)
- **Interface**: Presentation layer (ImGui rendering, mouse events, keyboard shortcuts)
- **GraphicsBackend**: Abstract GPU interface (Vulkan, OpenGL 2 implementations)
- **Prefetcher**: Background I/O thread for QC mode volume preloading

### Dependencies
- **MINC libraries** (FetchContent with system fallback):
  - `LIBMINC`: System first via `find_package`, fallback to FetchContent
  - `minc2-simple`: FetchContent from GitHub
- `ImGui` (FetchContent, docking branch)
- `GLFW` (system)
- `Vulkan` (system, optional — `ENABLE_VULKAN`)
- `OpenGL` (system, optional — `ENABLE_OPENGL2`)
- `nlohmann/json` (FetchContent, v3.11.3)
- `GLM` (FetchContent, v1.0.1)
- `Eigen` (FetchContent, v3.4.0 — for Levenberg-Marquardt optimizer)
- `HDF5` (system, required by MINC2)
- `Threads` (system, required for Prefetcher)
- `stb_image_write` (single header, in `include/`)

### Build
```bash
cd new_register/build
cmake -DENABLE_VULKAN=ON -DENABLE_OPENGL2=ON ..
make
```

### Tests
```bash
cd new_register/build
ctest --output-on-failure
```

### Test Data
- `test_data/mni_icbm152_t1_tal_nlin_sym_09c.mnc` — 1mm isotropic (193x229x193)
- `test_data/mni_icbm152_t1_tal_nlin_sym_09c_thick_slices.mnc` — 3x1x2mm (64x229x96)

---

## new_mincpik — Headless Mosaic Image Generator

A command-line companion to `new_register` that loads one or more MINC2 volumes,
composites overlay slices (same algorithm as the overlay panel), arranges them in
a grid mosaic, and writes the result to a `.png` file. No GPU, X11, Wayland, GLFW,
or ImGui dependencies.

### Architecture

```
new_register/
├── CMakeLists.txt              # nr_core library + new_mincpik target
├── include/
│   ├── SliceRenderer.h         # CPU slice/overlay rendering API
│   └── ... (existing headers)
├── src/
│   ├── SliceRenderer.cpp       # CPU rendering (ported from ViewManager)
│   ├── mincpik/
│   │   └── mincpik_main.cpp    # entry point + CLI + mosaic assembly
│   └── ... (existing sources)
└── tests/
    ├── test_mincpik.cpp        # rendering correctness tests
    └── ... (existing tests)
```

### Shared Library: `nr_core`

Static library of GPU-free modules shared by both `new_register` and `new_mincpik`:

| Source | Purpose |
|--------|---------|
| `Volume.cpp` | MINC2 loading, coordinate transforms |
| `ColourMap.cpp` | 21 colour map LUT generation |
| `Transform.cpp` | Tag-point registration (LSQ6-12, TPS) |
| `TagWrapper.cpp` | Tag file I/O |
| `AppConfig.cpp` | JSON config persistence |

`SliceRenderer.cpp` is also added to `nr_core` so both apps share it.

### SliceRenderer API

```cpp
struct VolumeRenderParams {
    double valueMin, valueMax;
    ColourMapType colourMap;
    float overlayAlpha;
    int underColourMode, overColourMode;
};

struct RenderedSlice {
    std::vector<uint32_t> pixels;  // packed 0xAABBGGRR
    int width, height;
};

RenderedSlice renderOverlaySlice(
    const std::vector<const Volume*>& volumes,
    const std::vector<VolumeRenderParams>& params,
    int viewIndex,
    const std::vector<int>& sliceIndices,
    const TransformResult* transform = nullptr);
```

Ported from the CPU portions of `ViewManager::updateOverlayTexture()` — same
scanline-optimized compositing, same colour mapping, same under/over clamping.

### Mosaic Layout

```
┌──────────┬──────────┬──────────┐
│ Coronal  │ Coronal  │ Coronal  │  row 0
│ slice 1  │ slice 2  │ slice 3  │
├──────────┼──────────┼──────────┤
│ Sagittal │ Sagittal │ Sagittal │  row 1
│ slice 1  │ slice 2  │ slice 3  │
├──────────┼──────────┼──────────┤
│ Axial    │ Axial    │ Axial    │  row 2
│ slice 1  │ slice 2  │ slice 3  │
└──────────┴──────────┴──────────┘
```

- Each row = one anatomical plane
- Each column = one slice at a given coordinate (evenly spaced or user-specified)
- Default: 1 slice per plane at volume center (3 rows x 1 column)
- Output via `stbi_write_png()`

### CLI

```
new_mincpik [options] volume1.mnc [volume2.mnc ...] -o output.png

Volume display (apply to next volume):
  -G, --gray              GrayScale colour map
  -H, --hot               HotMetal colour map
  -S, --spectral          Spectral colour map
  -r, --red               Red colour map
  -g, --green             Green colour map
  -b, --blue              Blue colour map
  --lut <name>            Named colour map
  --range <min>,<max>     Value range
  -l, --label             Mark as label volume
  -L, --labels <file>     Label description file

Slice selection:
  --axial <N>             Number of axial slices (default: 1)
  --sagittal <N>          Number of sagittal slices (default: 1)
  --coronal <N>           Number of coronal slices (default: 1)
  --axial-at <z1,z2,...>  World Z coordinates
  --sagittal-at <x1,...>  World X coordinates
  --coronal-at <y1,...>   World Y coordinates

Output:
  -o, --output <path>     Output PNG (required)
  --width <W>             Scale output to this width
  --gap <px>              Cell gap (default: 2)

Transform:
  -t, --tags <file>       Load .tag file
  --xfm <file>            Load .xfm transform

Other:
  -c, --config <path>     Load config.json
  --alpha <v1,v2,...>     Per-volume overlay alpha
  -h, --help
  -d, --debug
```

### Implementation Status
- [ ] Create `nr_core` static library in CMakeLists.txt
- [ ] Update `new_register` target to link `nr_core`
- [ ] Update test targets to link `nr_core`
- [ ] Verify build + all 14 tests pass
- [ ] Create `SliceRenderer.h/.cpp`
- [ ] Create `mincpik_main.cpp`
- [ ] Add `new_mincpik` target to CMakeLists.txt
- [ ] Build and test with real MINC data
- [ ] Write `test_mincpik.cpp`
- [ ] Verify all tests pass

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
| `src/Prefetcher.cpp` | Disable threading or enable pthread support (`-sUSE_PTHREADS=1`) |
| `src/Volume.cpp` | Use fetch + memory filesystem for file loading |

### Implementation Details

1. **Graphics Backend**: Use ImGui's OpenGL2 backend (works with WebGL2 in browser via `-sUSE_WEBGL2`)
2. **Window Management**: GLFW works with Emscripten; use `ImGui_ImplGlfw_InstallEmscriptenCallbacks()` for canvas resize
3. **Threading**: Option A) Disable Prefetcher thread in WASM; Option B) Use `-sUSE_PTHREADS=1 -sPROXY_TO_PTHREAD`
4. **File I/O**: Use `--preload-file` to bundle MINC files, or fetch via JavaScript and load into Emscripten's MEMFS

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
| Threading | Medium |

**Full implementation:** 1-2 weeks  
**Minimum viable:** 2-3 days (basic rendering, no threading, limited file loading)
