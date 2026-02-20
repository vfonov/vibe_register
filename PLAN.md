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
- [x] Tools panel with Save Local, Reset All Views, Quit buttons

### Command Line Options
- [x] `-c`, `--config <path>` — load config from a specific path
- [x] `-h`, `--help`
- [x] `--test` — launch with a generated test volume (no-input prints help and exits with code 1)
- [x] Positional volume file arguments
- [x] `-r`/`--red`, `-g`/`--green`, `-b`/`--blue`, `-G`/`--gray`, `-H`/`--hot`, `-S`/`--spectral`
- [x] `--lut <name>` — set any colour map by name for next volume
- [x] `-B`/`--backend <name>` — graphics backend: `auto`, `vulkan`, `opengl2` (default: auto)
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

### Keyboard Shortcuts
- [x] `R` — Reset All Views
- [x] `Q` — Quit
- [x] `C` — Toggle clean mode
- [x] `P` — Screenshot (save window as PNG)
- [x] `T` — Toggle tag list window
- [x] `+`/`-` — Step through axial (Z) slice

### Tag Points
- [x] Load/save `.tag` files via TagWrapper class
- [x] Tag list window with table (index, label, coordinates per volume)
- [x] Tag selection updates cursor position in all volumes
- [x] Right-click on slice to create new tag
- [x] Delete selected tag button
- [x] Tags window docked below Tools panel in left panel

### Screenshot
- [x] Capture entire window to PNG via `P` key or button
- [x] Auto-incrementing filenames, never overwrites
- [x] Works with both Vulkan and OpenGL 2 backends

### Tests
- [x] 12 test suites (volume loading, colour maps, transforms, coordinate sync, tags, QC CSV, etc.)

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
- [x] Radio buttons for transform type selection
- [x] Save `.xfm` files via minc2-simple API (linear) and hand-written (TPS)
- [x] Read `.xfm` files via minc2-simple API
- [x] User-editable `.xfm` output filename (InputText)
- [x] Save `.tag` button alongside Save `.xfm`
- [x] 11 transform subtests passing (identity, translation, rotation, scaling, mixed, all LSQ types, TPS, XFM I/O)

---

## Not Yet Implemented

### Tag Points (remaining)
- [ ] Edit tag labels in table
- [ ] Tag markers displayed on slices (inside/outside colours, active/inactive colours)
- [ ] Up/Down arrow keys to navigate between tags
- [ ] Load tags from command line

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
- [ ] NIfTI-1 format support
- [ ] RGB/vector volume support
- [ ] 4D volumes with time dimension

### UI Features Not Yet Ported
- [ ] Cursor visibility toggle
- [ ] Per-volume Load button / filename entry
- [ ] Quit confirmation dialog
- [ ] Save slice images to file

### Metal Backend (Future)
- [ ] `include/MetalBackend.h` / `src/MetalBackend.mm` (Objective-C++)
- [ ] CMake: `enable_language(OBJCXX)`, find Metal and QuartzCore frameworks
- [ ] GLFW: `GLFW_NO_API`, create `CAMetalLayer` on native Cocoa view
- [ ] ImGui: `ImGui_ImplGlfw_InitForOther()` + `ImGui_ImplMetal_Init(device)`
- [ ] Texture: `MTLTexture` objects, `ImTextureID = MTLTexture*`

### Remaining CLI Options
- [ ] `-sync`: start with volumes synced
- [ ] `-range VOLUME MIN MAX`: force initial colour range

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
- [x] Preserves display settings (colour map, value range, zoom, pan) across row switches
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
│   ├── TagWrapper.cpp
│   ├── Transform.cpp
│   ├── ViewManager.cpp
│   ├── Volume.cpp
│   ├── VulkanBackend.cpp
│   └── VulkanHelpers.cpp
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
    └── test_transform.cpp
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
