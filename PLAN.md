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

### Multi-Volume Synchronization
- [x] "Sync All" checkbox in Tools panel
- [x] Left-click/drag, middle-drag scroll, and mouse wheel zoom respect sync setting
- [x] Voxel-to-world and world-to-voxel matrices built correctly in `Volume::load()`
- [x] `SyncCursors()` uses simple voxel→world→voxel pipeline (no centers/steps heuristics)
- [x] World (0,0,0) maps to correct voxel for both test volumes
- [x] Fixed X↔Y swap bug in crosshairs, mouse clicks, sync, overlay, and info display
- [x] **Separate sync controls**: Sync Cursor, Sync Zoom, Sync Pan checkboxes
- [x] **Symmetric sync**: zoom/pan sync now works both ways (volume→overlay and overlay→volume)

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
- [x] **Fixed direction cosines**: overlay now uses proper `transformVoxelToWorld`/`transformWorldToVoxel` matrices
- [x] **Fixed out-of-bounds handling**: out-of-volume pixels show transparent background instead of clamped edge values

### Configuration
- [x] JSON config persistence using Glaze
- [x] Two-tier config: global (`~/.config/new_register/config.json`) and local (`./config.json`)
- [x] Per-volume paths, colour maps, value ranges, slice indices, zoom, pan
- [x] Tools panel with Save Global, Save Local, Reset All Views, Quit buttons
- [x] Tag list visibility saved/loaded in config

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
- [x] `T` — Toggle tag list window

### Clean Mode
- [x] Toggle via `C` key or "Clean Mode" button in Tools panel
- [x] Hides: Tools panel, per-volume controls (dimensions, colour maps, range inputs), overlay blend controls, slice navigation sliders (+/- buttons and slider bars)
- [x] Keeps: Volume window title bars (filenames), 3-plane slice views, crosshairs, overlay views

### Compact UI
- [x] Removed volume dimensions and voxel size display
- [x] Combined cursor position into single line: `V: i,j,k  W: x,y,z  I: value`
- [x] Removed "Slice" label from slice sliders

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
- [x] World↔voxel transform tests (`test_world_to_voxel.cpp`)
- [x] Coordinate sync tests (`test_coordinate_sync.cpp`)
- [x] Matrix debug dump tests (`test_matrix_debug.cpp`)
- [x] MINC dimension info tests (`test_minc_dims.cpp`)
- [x] Tag loading tests (`test_tag_load.cpp`)
- [x] Thick-slices center of mass test (`test_thick_slices_com.cpp`)
- [x] Center of mass test (`test_center_of_mass.cpp`)
- [x] Volume info test (`test_volume_info.cpp`)

---

## Not Yet Implemented

### Tag Points System
- [x] Load `.tag` files via TagWrapper class
- [x] Tag list window with table showing tag index, label, and coordinates per volume
- [x] Tag selection updates cursor position in all volumes
- [x] Right-click on slice to create new tag at each volume's current crosshair cursor position
- [x] Delete selected tag button
- [x] Tag list toggle via `T` keyboard shortcut
- [x] Tag list checkbox in Tools panel with `(T)` hint (state saved in config)
- [x] Fixed ImGui ID conflict in tag table selectable rows
- [ ] Edit tag labels in table
- [ ] Tag markers displayed on slices (inside/outside colours, active/inactive colours)
- [ ] Up/Down arrow keys to navigate between tags
- [ ] Per-tag RMS error display
- [ ] Average RMS error display

### Tag File I/O
- [x] Load `.tag` files (implemented via TagWrapper class with exception handling)
- [x] Save `.tag` files
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

### Interpolation (Future)
- [ ] Add interpolation methods: Nearest Neighbor, Linear (Trilinear), Cubic (Tricubic)
- [ ] Per-volume interpolation type selection
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

## QC (Quality Control) Mode

A special mode of operation for batch quality control of medical imaging datasets.
Tags are completely disabled. The UI is streamlined. Most configuration is via CLI
and a JSON config file. The user reviews datasets from a CSV file, rating each
volume column PASS/FAIL with optional comments.

### Example Usage
```bash
new_register --qc subjects.csv --qc-output results.csv --config qc_config.json
```

### Input CSV Format (fixed columns)
```csv
ID,T1,T2
sub01,/data/sub01_t1.mnc,/data/sub01_t2.mnc
sub02,/data/sub02_t1.mnc,/data/sub02_t2.mnc
sub03,/data/sub03_t1.mnc,/data/sub03_t2.mnc
```
- First column must be `ID`.
- Remaining columns are volume file paths; column names match config keys.
- All rows have the same columns. Missing files show an empty panel with error message.

### Output CSV Format (auto-saved on every verdict/comment change)
```csv
ID,T1_verdict,T1_comment,T2_verdict,T2_comment
sub01,PASS,,FAIL,motion artifact
sub02,,,,
sub03,PASS,,PASS,
```
- Generated columns: `ID`, then `{col}_verdict` and `{col}_comment` for each input column.
- If the output file already exists, it is read on startup to pre-populate verdicts/comments (matched by ID).

### Config JSON (`qc_config.json`)
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
- Uses existing `--config` flag and Glaze JSON config system.
- `qc_columns` maps CSV column names to per-column display settings.
- `showOverlay` controls whether the blended overlay panel appears (default: true).
- Colour map and value range are applied as initial defaults but remain adjustable at runtime.

### Keyboard Shortcuts (QC Mode)
| Key | Action |
|---|---|
| `]` | Next dataset |
| `[` | Previous dataset |
| `Up/Down` | Navigate QC list (when list is focused) |
| `Q` | Quit |
| `R` | Reset views |
| `C` | Clean mode |
| `P` | Screenshot |

### Implementation Plan

#### Phase 1: Core Data Structures and CSV I/O
- [x] **1.1** Create `QCState.h` — QC data structures
  - `QCVerdict` enum: `UNRATED`, `PASS`, `FAIL`
  - `QCColumnConfig` struct: `colourMap`, `valueMin`, `valueMax`
  - `QCRowResult` struct: `id`, `std::vector<QCVerdict> verdicts`, `std::vector<std::string> comments`
  - `QCState` class: all QC runtime state
    - `inputCsvPath`, `outputCsvPath`
    - `columnNames` (header columns excluding "ID")
    - `rowIds`, `rowPaths` (per-row file paths)
    - `results` (verdicts/comments, parallel with rows)
    - `columnConfigs` (from config JSON, keyed by column name)
    - `currentRowIndex`
    - `active` flag
- [x] **1.2** Create `QCState.cpp` — CSV parsing and saving
  - `loadInputCsv(path)`: parse header + rows, populate columnNames/rowIds/rowPaths
  - `loadOutputCsv(path)`: parse existing results, match by ID, pre-populate verdicts/comments
  - `saveOutputCsv()`: write results CSV with `std::ofstream` + `std::format`, auto-called on every change
  - Handle edge cases: missing files, empty cells, quoted strings with commas
  - Note: csv-parser library (v2.4.2) was dropped due to SIGFPE crash on small files; replaced with hand-rolled RFC 4180 parser
- [x] **1.3** Extend `AppConfig.h` / `AppConfig.cpp` — add QC column config
  - Add `std::optional<std::map<std::string, QCColumnConfig>> qcColumns` to `AppConfig`
  - Add `showOverlay` to `GlobalConfig`
  - Add `glz::meta` specialization for `QCColumnConfig`
- [x] **1.4** Add `QCState.cpp` to `CMakeLists.txt`
- [x] **1.5** Write unit test for CSV round-trip (`test_qc_csv.cpp`) — 6 tests passing

#### Phase 2: Volume Lifecycle Management
- [x] **2.1** Fix `VulkanTexture` destructor — call `cleanup(g_Device)` in destructor so `unique_ptr::reset()` properly releases Vulkan resources (image, memory, sampler, image view, descriptor set)
- [x] **2.2** Add volume unload/reload to `AppState`
  - `clearAllVolumes()`: destroy all textures, clear volumes/paths/names/viewStates/overlay
  - `loadVolumeSet(paths)`: clear, load new volumes, init view states (per-column config applied by caller)
- [x] **2.3** Add texture lifecycle methods to `ViewManager`
  - `initializeAllTextures()`: create slice textures for all volumes + overlay (refactored from main.cpp inline code)
  - `destroyAllTextures()`: destroy all slice + overlay textures

#### Phase 3: CLI and Mode Activation
- [x] **3.1** Add QC CLI arguments to `main.cpp`
  - `--qc <input.csv>` — activate QC mode, specify input CSV path
  - `--qc-output <output.csv>` — specify output results CSV path (required with `--qc`)
  - Parse in existing argument loop; set `qcState.active = true`
- [x] **3.2** Integrate QC startup into `main.cpp`
  - When QC mode active: skip normal volume loading from CLI positional args
  - Load input CSV, load output CSV if exists, load config for column configs
  - Auto-select first unrated row (or row 0), load its volumes via `loadVolumeSet()`
  - Pass `QCState` reference to `Interface`

#### Phase 4: QC User Interface
- [x] **4.1** QC Dataset List Window (`Interface::renderQCListWindow()`)
  - Scrollable table: row index, ID, overall status indicator
  - Color coding: green = all PASS, red = any FAIL, white/gray = unrated
  - Click row to switch dataset (triggers volume unload + reload)
  - Progress display in header: "QC: 12/50 rated"
- [x] **4.2** Global `[`/`]` keyboard shortcuts for prev/next dataset
  - Triggers `backend.waitIdle()` + `destroyAllTextures()` + `loadVolumeSet()` + `initializeAllTextures()`
  - Fixed VK_ERROR_DEVICE_LOST crash by adding GPU sync before texture teardown
- [x] **4.3** Per-column verdict panel (below each volume's slice views)
  - Radio buttons: PASS / FAIL (UNRATED as default cleared state)
  - Text input field for comment
  - On any change: update `QCState::results`, call `saveOutputCsv()`
- [x] **4.4** QC mode UI restrictions
  - Hide: tag list window, tag checkbox, right-click tag creation, tag visibility toggle
  - Hide: "Save Local" / "Save Global" config buttons (config is read-only in QC)
  - Hide: volume path/load controls
  - Show: QC progress info, sync checkboxes, Reset View, colour map (adjustable), Quit
- [x] **4.5** Layout adjustments
  - QC list window docked on the left (alongside or replacing Tools panel)
  - Volume panels one column per CSV column, verdict panels below each
  - Overlay panel shown/hidden based on `showOverlay` config

#### Phase 5: Polish and Edge Cases
- [x] **5.1** Missing file handling — empty/placeholder panel with error message, log to stderr
- [x] **5.2** First-unrated-row jump — on startup, auto-scroll to first unrated row
- [x] **5.3** Clean mode interaction — `C` key hides everything except slices + verdict panels
- [x] **5.4** Proper flush on quit — verify output CSV is written before exit

### QC Mode File Changes Summary
| File | Action | Description |
|---|---|---|
| `include/QCState.h` | **New** | QC data structures, enums, state class |
| `src/QCState.cpp` | **New** | CSV parsing, saving, row management |
| `tests/test_qc_csv.cpp` | **New** | Unit test for CSV round-trip |
| `include/AppConfig.h` | Edit | Add `qcColumns`, `showOverlay` |
| `src/AppConfig.cpp` | Edit | Glaze meta for QCColumnConfig |
| `include/VulkanHelpers.h` | Edit | Fix VulkanTexture destructor |
| `src/VulkanHelpers.cpp` | Edit | Implement cleanup in destructor |
| `include/AppState.h` | Edit | Add `clearAllVolumes()`, `loadVolumeSet()`, QCState ref |
| `src/AppState.cpp` | Edit | Implement volume lifecycle methods |
| `include/ViewManager.h` | Edit | Declare `initializeAllTextures()`, `destroyAllTextures()` |
| `src/ViewManager.cpp` | Edit | Implement texture lifecycle methods |
| `include/Interface.h` | Edit | Add QC rendering methods |
| `src/Interface.cpp` | Edit | QC list window, verdict panels, mode restrictions |
| `src/main.cpp` | Edit | QC CLI args, startup integration |
| `CMakeLists.txt` | Edit | Add QCState.cpp and test to build |

---

## Architecture Notes

### Current File Structure
```
new_register/
├── CMakeLists.txt
├── include/
│   ├── AppConfig.h
│   ├── AppState.h
│   ├── ColourMap.h
│   ├── GraphicsBackend.h
│   ├── Interface.h
│   ├── QCState.h
│   ├── TagWrapper.hpp
│   ├── Volume.h
│   ├── ViewManager.h
│   ├── VulkanBackend.h
│   └── VulkanHelpers.h
├── src/
│   ├── main.cpp
│   ├── AppState.cpp
│   ├── ViewManager.cpp
│   ├── Interface.cpp
│   ├── VulkanBackend.cpp
│   ├── ColourMap.cpp
│   ├── VulkanHelpers.cpp
│   ├── Volume.cpp
│   ├── TagWrapper.cpp
│   ├── QCState.cpp
│   └── AppConfig.cpp
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
    └── test_qc_csv.cpp
```

### Code Quality Improvements
- [x] Migrated C-style arrays (`int[3]`, `double[3]`) to GLM vector types (`glm::ivec3`, `glm::dvec3`, `glm::dmat3`)
  - Volume class: `dimensions`, `step`, `start`, `dirCos` → GLM types
  - VolumeViewState/OverlayState: `sliceIndices`, `zoom`, `panU`, `panV`, `dragAccum` → GLM types
  - Transform functions: `transformVoxelToWorld()`, `transformWorldToVoxel()`, `worldExtent()` → GLM types
  - Test files updated to use GLM types
  - Note: `valueRange` kept as `float[2]` for ImGui compatibility, config serialization uses `std::array`

- [x] Standardized spatial index order to MINC convention (index 0 = X, index 1 = Y, index 2 = Z)
  - Changed `sliceIndices` from custom "app convention" (`[0]=Z,[1]=X,[2]=Y`) to MINC order (`[0]=X,[1]=Y,[2]=Z`)
  - Updated all crosshair drawing, mouse click handling, overlay crosshair, overlay mouse click
  - Updated info display to use direct MINC order
  - Removed sync conversion code (now uses direct world coordinate pipeline)
  - Updated config serialization/deserialization
  - Removed "app convention" comments throughout codebase

- [x] Fixed test files to return non-zero on failure
  - `test_minc_dims.cpp`: Added assertions for dimension IDs, lengths, steps, start values, world→voxel transformation
  - `test_volume_info.cpp`: Added assertions for ndim, dimensions, steps, start values, coordinate transformations
  - `test_matrix_debug.cpp`: Added assertions for dimensions, steps, start values, round-trip transformation, cross-volume sync

- [x] Refactored main.cpp into AppState, ViewManager, and Interface components
  - AppState: Data layer (volumes, view states, config)
  - ViewManager: Logic layer (texture generation, cursor sync)
  - Interface: Presentation layer (ImGui rendering, mouse events)

### Dependencies
- **MINC libraries** (FetchContent with system fallback):
  - `LIBMINC`: Try system first via `find_package` with `MINC_TOOLKIT` env var hints, fallback to FetchContent from `https://github.com/BIC-MNI/libminc.git` (develop branch)
  - `minc2-simple`: FetchContent from `https://github.com/NIST-MNI/minc2-simple.git` (develop branch)
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

### Test Data
- `test_data/mni_icbm152_t1_tal_nlin_sym_09c.mnc` — 1mm isotropic (193x229x193), X/Y/Z
- `test_data/mni_icbm152_t1_tal_nlin_sym_09c_thick_slices.mnc` — 3x1x2mm (64x229x96)
- Expected world (0,0,0) in MINC X,Y,Z order: voxel (96,132,78) for 1mm volume, (32,132,39) for thick-slices
