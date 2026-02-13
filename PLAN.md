# PLAN.md — new_register Rewrite Status

## Overview

Modern C++23 rewrite of the legacy `register` application using Vulkan, ImGui (Docking), GLFW, and Glaze.

---

## Completed Features

### Volume Management
- [x] MINC2 volume loading via `minc2-simple`
- [x] Multi-volume support (CLI and config)
- [x] Synthetic test data fallback when no volumes specified
- [x] Per-volume min/max computation
- [x] World-space extent and pixel aspect ratio calculations

### Slice Viewing
- [x] 3-plane slice views per volume (transverse, sagittal, coronal)
- [x] Correct aspect ratio accounting for non-uniform voxel spacing
- [x] Slice navigation via slider and +/- buttons
- [x] Yellow crosshair overlay showing other views' slice positions

### Mouse Interaction
- [x] Left click/drag: set cross-slice positions
- [x] Middle drag: scroll through slices
- [x] Shift+Left drag: pan
- [x] Shift+Middle drag: zoom
- [x] Mouse wheel: zoom centered on cursor

### Colour Maps
- [x] 21 colour map types (Gray, HotMetal, Spectral, Red, Green, Blue, negative variants, Contour, etc.)
- [x] Quick-access swatch buttons (Gray, R, G, B, Spectral) with gradient preview
- [x] "More..." dropdown for all 21 maps
- [x] Per-volume colour map selection
- [x] Configurable under/over range colours (Current, Transparent, or any colour map)

### Value Range Controls
- [x] Min/max input fields per volume
- [x] Auto range button

### Overlay / Merged View
- [x] Alpha-blended composite of all volumes (when >1 volume loaded)
- [x] World-coordinate resampling (nearest-neighbour)
- [x] Per-volume alpha sliders (3+ volumes)
- [x] Single blend slider for two-volume case
- [x] Own zoom/pan/crosshair controls

### Configuration
- [x] JSON config persistence using Glaze
- [x] Two-tier config: global (`~/.config/new_register/config.json`) and local (`./config.json`)
- [x] Per-volume paths, colour maps, value ranges, slice indices, zoom, pan
- [x] Tools panel with Save Global, Save Local, Reset All Views, Quit buttons

### Command Line Options
- [x] `-c`, `--config <path>` — load config from a specific path
- [x] `-h`, `--help`
- [x] Positional volume file arguments
- [x] `-r`/`--red`, `-g`/`--green`, `-b`/`--blue` — set colour map for next volume
- [x] `-G`/`--gray`, `-H`/`--hot`, `-S`/`--spectral` — set colour map for next volume
- [x] `--lut <name>` — set any colour map by name for next volume
- LUT flags apply to the next volume file; CLI overrides config values
- Example: `new_register --gray vol1.mnc -r vol2.mnc`

### Graphics / Rendering
- [x] Vulkan backend with full lifecycle management
- [x] ImGui docking + viewports
- [x] HiDPI support (GLFW content scale query, ImGui style/font scaling)
- [x] Auto-layout: Tools panel on the left, one column per volume + overlay column
- [x] GPU texture creation, upload, and destruction helpers
- [x] Swapchain resize handling (`VK_ERROR_OUT_OF_DATE_KHR`, `VK_SUBOPTIMAL_KHR`)

### Keyboard Shortcuts
- [x] `R` — Reset All Views
- [x] `Q` — Quit
- [x] `C` — Toggle clean mode (hides Tools panel, volume controls, overlay controls; keeps only slice views with crosshairs and window titles)
- [x] `P` — Screenshot (save window as PNG to current directory)

### Clean Mode
- [x] Toggle via `C` key or "Clean Mode" button in Tools panel
- [x] Hides: Tools panel, per-volume controls (dimensions, colour maps, range inputs), overlay blend controls, slice navigation sliders (+/- buttons and slider bars)
- [x] Keeps: Volume window title bars (filenames), 3-plane slice views, crosshairs, overlay views

### Screenshot
- [x] Capture entire window to PNG via `P` key or "Screenshot" button in Tools panel
- [x] Auto-incrementing filenames (`screenshot000001.png`, `screenshot000002.png`, ...) — never overwrites
- [x] Prints filename to console on save
- [x] Vulkan framebuffer readback with automatic BGRA→RGBA swizzle
- [x] Uses `stb_image_write.h` for PNG encoding

### Error Handling
- [x] Exception-based error handling throughout
- [x] Top-level exception handler with proper cleanup

### Tests
- [x] Volume loading tests (`test_real_data.cpp`)
- [x] Colour map LUT tests (`test_colour_map.cpp`, 11 test cases)

---

## Not Yet Implemented

### Tag Points System
- [ ] Create, delete, navigate tag points
- [ ] Per-tag: position per volume (world coords), name, activity (On/Ignore)
- [ ] Tag markers displayed on slices (inside/outside colours, active/inactive colours)
- [ ] Tag visibility toggle
- [ ] Scrollable tag list with First/Prev/Next/Last navigation
- [ ] Right-click to add tag at cursor position
- [ ] Up/Down arrow keys to navigate between tags
- [ ] Tag positions editable via text fields
- [ ] Click tag to jump cursor to that position
- [ ] Per-tag RMS error display
- [ ] Average RMS error display

### Tag File I/O
- [ ] Load `.tag` files
- [ ] Save `.tag` files
- [ ] Load tags from command line

### Transform Computation
- [ ] Compute transform from tag point pairs (volumes 0 and 1)
- [ ] Minimum 4 valid tag points required
- [ ] Transform types: LSQ6, LSQ7, LSQ9, LSQ10, LSQ12 (affine), TPS (thin-plate spline)
- [ ] Transform auto-recomputed when tags change
- [ ] Save `.xfm` transform files
- [ ] Transform type selection dialog

### Resampling
- [ ] Resample volume 2 into volume 1's space using computed transform
- [ ] Progress indicator during resampling
- [ ] Auto-load resampled volume after completion

### Slice Filtering
- [ ] Per-view filter type: Nearest Neighbour, Linear, Box, Triangle, Gaussian
- [ ] Configurable FWHM (Full-Width Half-Maximum)
- [ ] Filter selection dialog per volume

### Interpolation
- [ ] Flat (nearest neighbour) vs Smooth (linear) toggle
- [ ] Global toggle affecting all views

### Merge Modes
- [ ] Per-volume merge mode: Disabled, Blended, Opaque
- [ ] Opaque volumes drawn on top of blended volumes

### Additional Volume Support
- [ ] Support up to 8 volumes (legacy supports `N_VOLUMES = 8`)
- [ ] NIfTI-1 format support
- [ ] MGZ format support
- [ ] RGB/vector volume support
- [ ] 4D volumes with time dimension (step through time, timecourse display)
- [ ] Progressive/incremental volume loading
- [ ] Volume caching for large volumes (>80MB)

### UI Features Not Yet Ported
- [ ] Volume sync toggle (world coordinates shared between volumes)
- [ ] Cursor visibility toggle
- [ ] Voxel and World position readouts (editable text fields)
- [ ] Volume value readout at cursor
- [ ] Per-volume Load button / filename entry
- [ ] Quit confirmation dialog
- [ ] Delete all tags confirmation dialog
- [ ] Save slice images to file

### Timecourse Window (4D volumes)
- [ ] Graph voxel value over time
- [ ] Y-axis: Full range / Scaled toggle
- [ ] Adjustable T(min) and T(max) range
- [ ] Save timecourse data to file

### Remaining Command Line Options
- [ ] `-sync`: start with volumes synced
- [ ] `-range VOLUME MIN MAX`: force initial colour range

---

## Architecture Notes

### Current File Structure
```
new_register/
├── CMakeLists.txt
├── include/
│   ├── AppConfig.h
│   ├── ColourMap.h
│   ├── GraphicsBackend.h
│   ├── Volume.h
│   ├── VulkanBackend.h
│   └── VulkanHelpers.h
├── src/
│   ├── main.cpp          (2102 lines — UI, state, rendering, main loop)
│   ├── VulkanBackend.cpp  (710 lines)
│   ├── ColourMap.cpp      (395 lines)
│   ├── VulkanHelpers.cpp  (322 lines)
│   ├── Volume.cpp         (210 lines)
│   └── AppConfig.cpp      (149 lines)
└── tests/
    ├── test_real_data.cpp
    └── test_colour_map.cpp
```

Total: ~3886 lines of application code.

### Suggested Refactoring
- `main.cpp` (2102 lines) should be decomposed into separate modules:
  - `SliceView` — per-slice rendering, mouse interaction, crosshairs
  - `OverlayView` — multi-volume compositing and overlay panel
  - `UIState` — application state, volume collection, view state management
  - `main.cpp` — main loop, window setup, Tools panel only
- Overlay compositing is currently CPU-side (per-pixel loop); consider GPU compute shader for large volumes.

### Dependencies
- `minc2-simple` (static, from `legacy/minc2-simple`)
- `ImGui` (FetchContent, docking branch)
- `GLFW` (system)
- `Vulkan` (system)
- `Glaze` (FetchContent, v4.2.3)
- `HDF5` (system, required by MINC2)
- `stb_image_write` (single header, in `include/`)

### Build
```bash
cd new_register/build
cmake ..
make
```

### Tests
```bash
cd new_register/build
ctest --output-on-failure
```
