# new_register

A multi-volume MINC2/NIfTI viewer built with ImGui (Docking).
Supports multiple graphics backends (Vulkan, OpenGL 2 on Linux/Windows;
Metal on macOS), side-by-side volume comparison, tag point management, and a
batch QC (Quality Control) review mode.

The same build also produces:

- **`new_qc`** — a lightweight standalone viewer for QC review of arbitrary images (see [below](#new_qc--image-qc-viewer)).
- **`new_mincpik`** — a headless mosaic PNG generator for MINC2 volumes (`new_mincpik --help`).
- **`mriv`** — a terminal slice viewer (see `mriv/PLAN.md`).

## Features

- MINC2 (`.mnc`, via minc2-simple) and NIfTI (`.nii`, `.nii.gz`, via vendored niftilib) volume loading; any number of volumes side by side
- Three orthogonal slice views per volume (axial, sagittal, coronal)
- Crosshair navigation with optional cursor / zoom / pan sync across volumes
- 18 colour maps (plus inversion), per-volume display range, zoom/pan per view
- Overlay compositing (volume 2 alpha-blended over volume 1)
- Tag point placement, editing, and `.tag` file I/O
- Registration transforms from tags: LSQ6/7/9/10/12 and thin-plate spline (TPS)
- QC batch review mode
- Screenshot capture (`P` key → `screenshot000001.png`, auto-incrementing)
- JSON config persistence (`config.json`)
- Hotkey reference panel in the UI

## Building (Linux)

```bash
cd new_register
mkdir build && cd build
cmake ..
make -j$(nproc)
ctest --output-on-failure    # optional: run the test suite
```

Requires a C++17 compiler (GCC 9+) and GLFW3 (system package, or `-DFETCH_GLFW=ON`).
See [Dependencies](#dependencies) for the rest.

## Building (macOS)

On macOS the app uses a native Cocoa window with a Metal renderer
(`imgui_impl_osx` + `imgui_impl_metal`); GLFW, Vulkan, and OpenGL are not used.
CMake auto-detects macOS (`APPLE`) and selects the Metal backend, disables the
GLFW/Vulkan/OpenGL paths and the OSMesa headless tests. `new_qc` also has a
native Cocoa+Metal path on macOS (`src/qc/main_macos.mm`).

```bash
# Xcode Command Line Tools provide the Metal/MetalKit/AppKit frameworks.
xcode-select --install        # if not already installed
brew install hdf5 netcdf      # MINC stack (minc2-simple/libminc build from source)

cd new_register
mkdir build && cd build
cmake ..
make -j$(sysctl -n hw.ncpu)
./new_register ../test_data/<volume>.mnc
```

The Metal backend is selected automatically; `--backend metal` is the only
accepted backend value on macOS.

### Dependencies

| Dependency | How it is obtained | Notes |
|---|---|---|
| `minc2-simple` | FetchContent from GitHub (develop branch) | MINC2 I/O |
| `libminc` | From minc-toolkit if present, otherwise ExternalProject | |
| HDF5, NetCDF, zlib | From minc-toolkit if present, otherwise system | See [Building against minc-toolkit](#building-against-minc-toolkit) |
| ImGui | Fetched by CMake (Docking branch) | |
| GLFW | System (or `FETCH_GLFW=ON` for 3.4) | Not used on macOS. 3.4 fixes Wayland touch events |
| Vulkan | System, optional | OpenGL 2 fallback available |
| nlohmann/json | System package preferred; FetchContent v3.11.3 fallback | |
| Eigen | System package preferred (3.3+ or 5.x); ExternalProject 5.0.1 fallback | Transform math |
| glm | System package preferred; FetchContent 1.0.1 fallback | Vector/matrix types |
| niftilib | Vendored in `third_party/niftilib/` | NIfTI I/O |
| notcurses | System, or self-built with `MRIV_BUILD_NOTCURSES=ON` | `mriv` only |
| cxxopts | FetchContent | `mriv` only |

### Build Options

| CMake option | Default | Description |
|---|---|---|
| `ENABLE_VULKAN` | ON (OFF on macOS) | Build the Vulkan backend (needs `libvulkan-dev`) |
| `ENABLE_OPENGL2` | ON (OFF on macOS) | Build the OpenGL 2 backend (needs `libgl-dev`) |
| `ENABLE_METAL` | OFF (ON on macOS) | Build the Metal backend |
| `ENABLE_TESTS` | ON (OFF on macOS) | Build the headless UI tests (requires OSMesa) |
| `BUILD_NEW_QC` | ON | Build `new_qc` alongside `new_register` |
| `BUILD_QC_ONLY` | OFF | Build only `new_qc`, skipping the MINC/HDF5/Eigen dependencies |
| `BUILD_TERMINAL_VIEWER` | ON | Build the `mriv` terminal viewer |
| `MRIV_BUILD_NOTCURSES` | OFF | Download, build, and statically link notcurses instead of using a system install |
| `FETCH_GLFW` | OFF | Always fetch GLFW 3.4 from source, ignoring any system installation |

For example:

```bash
cmake -DENABLE_VULKAN=ON -DENABLE_OPENGL2=OFF ..  # Vulkan only
cmake -DENABLE_VULKAN=OFF -DENABLE_OPENGL2=ON ..  # OpenGL 2 only
```

### Building against minc-toolkit

CMake looks for a minc-toolkit install (a prefix containing `bin/mincstats`) via
`MINC_TOOLKIT_DIR`, honouring `$MINC_TOOLKIT` and falling back to `/opt/minc/*`.
With no toolkit present, the build uses system HDF5 plus libminc via ExternalProject.

A toolkit ships its own HDF5, NetCDF and zlib, and libminc is tightly coupled to
that exact HDF5 build. When a toolkit is found, the build pins HDF5/NetCDF/zlib
to the toolkit's copies and never consults the system ones — including inside the
`minc2-simple` and `libminc` sub-builds.

This is not a preference. The HDF5 that ships with minc-toolkit 1.9.18.9 (1.10.6)
and Ubuntu's `libhdf5-dev` (1.10.10) use *different include guards* in
`H5public.h` — `_H5public_H` versus `H5public_H` — so if both include directories
are on the compiler's path, both headers are included and every HDF5 enum and
struct is declared twice. The build then fails with a wall of
`redeclaration of enumerator 'H5_ITER_UNKNOWN'` errors from `minc2-simple`.

A relocated toolkit's exported CMake config (`LIBMINC-targets.cmake`) bakes in the
original build machine's absolute paths, so the build resolves libminc from the
install prefix directly rather than through that config.

To check which HDF5 a build picked up:

```bash
cmake .. 2>&1 | grep -E "MINC toolkit|HDF5|zlib|NETCDF"
ldd new_register | grep -E "hdf5|minc|libz"
```

### Offline builds

Dependencies that would otherwise be cloned from git can be supplied as
pre-fetched source trees:

```bash
cmake -DEIGEN_SOURCE_DIR=/path/to/eigen \
      -DLIBMINC_SOURCE_DIR=/path/to/libminc \
      -DMINC2_SIMPLE_SOURCE_DIR=/path/to/minc2-simple ..
```

## Command Line Usage

```
new_register [options] [volume1.mnc volume2.nii.gz ...]
```

### Options

| Flag | Argument | Description |
|---|---|---|
| `-G`, `--gray` | | Gray colour map for the next volume |
| `-H`, `--hot` | | Hot Metal colour map for the next volume |
| `-S`, `--spectral` | | Spectral colour map for the next volume |
| `-r`, `--red` | | Red colour map for the next volume |
| `-g`, `--green` | | Green colour map for the next volume |
| `-b`, `--blue` | | Blue colour map for the next volume |
| `--lut` | `<name>` | Named colour map for the next volume (see [Available Colour Maps](#available-colour-maps)) |
| `--range` | `<min,max>` | Display value range for the next volume |
| `-l`, `--label` | | Mark the next volume as a label volume |
| `-L`, `--labels` | `<file>` | Label description file for the next volume |
| `-c`, `--config` | `<path>` | Load config JSON from `<path>` |
| `-B`, `--backend` | `<name>` | Graphics backend: `auto` (default), `vulkan`, `opengl2`, `metal` (macOS) |
| `-t`, `--tags` | `<file>` | Load a combined two-volume `.tag` file |
| `-d`, `--debug` | | Enable debug output |
| `-h`, `--help` | | Show help and exit |
| `--test` | | Launch with a generated test volume |
| `--scale` | `<factor>` | Override screen content scale (HiDPI) |
| `--qc` | `<input.csv>` | QC mode, one verdict per column (see below) |
| `--qc1` | `<input.csv>` | QC mode, a single verdict per row |
| `--qc-output` | `<output.csv>` | Output CSV for QC verdicts (required with `--qc` / `--qc1`) |
| `--sync` | | Synchronize cursor, zoom and pan across volumes |
| `--sync-cursor` | | Synchronize cursor position across volumes |
| `--sync-zoom` | | Synchronize zoom level across volumes |
| `--sync-pan` | | Synchronize pan position across volumes |

Positional arguments are volume file paths: `.mnc`, `.mnc.gz`, `.nii`, `.nii.gz`.
Colour-map, range and label flags apply to the next volume on the command line.

Running with no arguments prints help and exits. Use `--test` to launch with
a generated test volume.

### Examples

View a single volume:

```bash
./new_register ../test_data/mni_icbm152_t1_tal_nlin_sym_09c.mnc
```

View two volumes side-by-side with different colour maps:

```bash
./new_register --gray --range 0,100 vol1.mnc --hot vol2.mnc
```

Load a saved configuration:

```bash
./new_register --config my_config.json
```

Force a specific graphics backend:

```bash
./new_register --backend opengl2 vol1.mnc
```

Run in QC mode:

```bash
./new_register --qc input.csv --qc-output results.csv --config qc_config.json
```

## Keyboard Shortcuts

| Key | Action | Mode |
|---|---|---|
| `R` | Reset all views (zoom, pan, slice positions) | All |
| `Q` | Quit | All |
| `C` | Toggle clean mode (hide control panels) | All |
| `P` | Save screenshot (`screenshot000001.png`, auto-incrementing) | All |
| `T` | Toggle tag list window | Normal only |
| `]` | Next QC dataset | QC only |
| `[` | Previous QC dataset | QC only |

### Mouse Controls

| Input | Action |
|---|---|
| Left click | Move crosshair to clicked position |
| Shift + Left drag | Pan the view |
| Middle drag | Scroll through slices |
| Shift + Middle drag | Zoom in/out |
| Scroll wheel | Zoom in/out (cursor-centered) |
| Right click | Create a tag point (normal mode only) |

## Configuration

Configuration is stored as JSON. The config file is loaded from `./config.json`
in the current directory, or from a path specified with `--config`.

The "Save Local" button in the UI writes to the active config path.

### config.json (Regular Mode)

```json
{
    "global": {
        "default_colour_map": "Gray",
        "window_width": 1600,
        "window_height": 900,
        "sync_cursors": true,
        "sync_zoom": false,
        "sync_pan": false,
        "tag_list_visible": false,
        "show_overlay": true
    },
    "volumes": [
        {
            "path": "test_data/mni_icbm152_t1_tal_nlin_sym_09c.mnc",
            "colour_map": "Gray",
            "value_min": 0.0,
            "value_max": 100.0,
            "slice_indices": [96, 132, 78],
            "zoom": [1.0, 1.0, 1.0],
            "pan_u": [0.5, 0.5, 0.5],
            "pan_v": [0.5, 0.5, 0.5]
        },
        {
            "path": "test_data/mni_icbm152_t1_tal_nlin_sym_09c_r.mnc",
            "colour_map": "Hot Metal"
        }
    ]
}
```

### Global Config Fields

| JSON key | Type | Default | Description |
|---|---|---|---|
| `default_colour_map` | string | `"Gray"` | Default colour map for new volumes |
| `window_width` | int or null | auto | Initial window width in pixels |
| `window_height` | int or null | auto | Initial window height in pixels |
| `sync_cursors` | bool | `false` | Synchronize crosshair position across volumes |
| `sync_zoom` | bool | `false` | Synchronize zoom level across volumes |
| `sync_pan` | bool | `false` | Synchronize pan position across volumes |
| `tag_list_visible` | bool | `false` | Show the tag list window on startup |
| `show_overlay` | bool | `true` | Show the overlay comparison panel |

### Volume Config Fields

| JSON key | Type | Default | Description |
|---|---|---|---|
| `path` | string | (required) | Path to the volume file |
| `colour_map` | string | `"Gray"` | Colour map display name |
| `invert_colour_map` | bool | `false` | Invert (reverse) the colour map |
| `value_min` | double or null | auto | Display range minimum |
| `value_max` | double or null | auto | Display range maximum |
| `slice_indices` | [int, int, int] | `[-1,-1,-1]` | Initial slice positions (-1 = midpoint) |
| `zoom` | [double, double, double] | `[1,1,1]` | Per-view zoom levels (sagittal, coronal, axial) |
| `pan_u` | [double, double, double] | `[0.5,0.5,0.5]` | Per-view horizontal pan center |
| `pan_v` | [double, double, double] | `[0.5,0.5,0.5]` | Per-view vertical pan center |

All volume fields except `path` are optional; omitted fields use their defaults.

### Available Colour Maps

Use these display names in the `colour_map` JSON field and the `--lut` CLI flag.
Any map can be inverted (`invert_colour_map`, or the invert control in the UI).

| Name | Description |
|---|---|
| `Gray` | Grayscale (black to white) |
| `Hot Metal` | Black-red-yellow-white |
| `Cold Metal` | Black-blue-cyan-white |
| `Green Metal` | Black-green-white |
| `Lime Metal` | Black-lime-white |
| `Red Metal` | Black-red-white |
| `Purple Metal` | Black-purple-white |
| `Spectral` | Rainbow spectrum |
| `Red` | Red channel only |
| `Green` | Green channel only |
| `Blue` | Blue channel only |
| `Contour` | Contour lines |
| `Viridis` | Perceptually uniform, blue-green-yellow |
| `Jet` | Blue-cyan-yellow-red rainbow |
| `Magma` | Perceptually uniform, black-purple-orange-white |
| `Inferno` | Perceptually uniform, black-purple-orange-yellow |
| `Plasma` | Perceptually uniform, blue-purple-yellow |
| `Turbo` | Improved rainbow |

## QC Mode

QC mode is a batch review workflow for rating medical imaging datasets.
Users step through rows of a CSV file, viewing the volumes for each row
side by side, and marking each column as PASS or FAIL with optional
comments. Verdicts are auto-saved to the output CSV on every change.

Use `--qc1` instead of `--qc` to record a single verdict per row rather than
one per column:

```bash
./new_register --qc1 input.csv --qc-output results.csv
```

### QC Input CSV Format

The first column must be `ID`. Remaining columns are volume file paths,
one column per volume to display:

```csv
ID,T1w,mask
sub-001,/data/sub-001/t1.mnc,/data/sub-001/mask.mnc
sub-002,/data/sub-002/t1.mnc,/data/sub-002/mask.mnc
sub-003,/data/sub-003/t1.mnc,/data/sub-003/mask.mnc
```

- The header row is mandatory.
- Column names (e.g. `T1w`, `mask`) are used as panel titles and as keys
  in the QC config.
- File paths can be absolute or relative to the working directory.
- Empty cells are treated as missing volumes (displayed with an error message).
- The parser follows RFC 4180 (supports quoted fields, commas in values, etc.).

### QC Output CSV Format

The output CSV is written automatically. For each input column, two output
columns are generated: `<name>_verdict` and `<name>_comment`:

```csv
ID,T1w_verdict,T1w_comment,mask_verdict,mask_comment
sub-001,PASS,,FAIL,registration artifact
sub-002,PASS,,PASS,
sub-003,,,,
```

- Verdict values: `PASS`, `FAIL`, or empty (unrated).
- If the output file already exists when QC mode starts, previous verdicts
  are loaded and the viewer jumps to the first unrated row.

### QC Config (qc_config.json)

Use `--config qc_config.json` to set per-column display parameters:

```json
{
    "global": {
        "show_overlay": false,
        "sync_cursors": true
    },
    "qc_columns": {
        "T1w": {
            "colour_map": "Gray",
            "value_min": 0.0,
            "value_max": 100.0
        },
        "mask": {
            "colour_map": "Red"
        }
    }
}
```

The keys under `qc_columns` must match the CSV column headers exactly.

### QC Column Config Fields

| JSON key | Type | Default | Description |
|---|---|---|---|
| `colour_map` | string | `"Gray"` | Colour map for this column |
| `value_min` | double or null | auto | Display range minimum |
| `value_max` | double or null | auto | Display range maximum |

### QC Mode Behavior

- Tags are completely disabled (no creation, no list, no `T` key).
- "Save Local" button is hidden.
- The QC list panel on the left shows all rows with colour-coded status
  (green = all pass, red = any fail, gray = unrated).
- Navigate with `[`/`]` keys or by clicking rows in the QC list.
- Verdict panels appear below each volume's slice views.
- Clean mode (`C`) hides control panels but keeps verdict panels visible.
- On quit, the output CSV is flushed before shutdown.

### Running the Example

A sample QC CSV is provided in `examples/qc_example.csv`:

```bash
./build/new_register \
    --qc examples/qc_example.csv \
    --qc-output examples/qc_example_out.csv
```

## new_qc — Image QC Viewer

`new_qc` is a lightweight standalone viewer for reviewing arbitrary images
(PNG, JPG, etc.) listed in a CSV manifest. It does not need the MINC stack —
build it alone with `-DBUILD_QC_ONLY=ON` on machines without one. On macOS it
uses its own native Cocoa + Metal path.

```bash
./new_qc [--scale <factor>] [--backend vulkan|opengl2] input.csv output.csv
```

### new_qc CSV Formats

Input — `id,visit,picture`:

```csv
id,visit,picture
subject001,baseline,/path/to/image1.png
subject002,followup,/path/to/image2.jpg
```

Output — `id,visit,picture,QC,notes`:

```csv
id,visit,picture,QC,notes
subject001,baseline,/path/to/image1.png,Pass,Good quality
subject002,followup,/path/to/image2.jpg,Fail,Artifact present
```

If the output CSV already exists, earlier results are loaded and review resumes.

### Stdin (simplified) mode

Pass `-` as the input CSV to read a newline-separated list of image paths from
standard input; the table is built on the fly:

```bash
find images/ -name '*.png' | ./new_qc -       # output defaults to qc_results.csv
ls *.jpg | ./new_qc - results.csv
```

Blank lines are skipped. Each path becomes a record with `id` = filename stem
(last extension stripped), `visit` = `-`, and `picture` = the path as given.
`output_csv` is optional in this mode and defaults to `qc_results.csv` in the
current directory; resuming from an existing output CSV still takes precedence.

### new_qc Keyboard

| Key | Action |
|---|---|
| `P` | Pass (auto-advance) |
| `F` | Fail (auto-advance) |
| `←` / `Page Up` | Previous image |
| `→` / `Page Down` | Next image |
| `Ctrl+S` | Save |
| `Esc` | Exit |
