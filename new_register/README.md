# new_register

A multi-volume MINC2 viewer built with Vulkan, ImGui (Docking), and GLFW.
Supports side-by-side volume comparison, tag point management, and a batch
QC (Quality Control) review mode.

## Building

```bash
cd new_register
mkdir build && cd build
cmake ..
make -j$(nproc)
```

Requires system packages: Vulkan SDK, GLFW3, and a C++23 compiler.

## Command Line Usage

```
new_register [options] [volume1.mnc volume2.mnc ...]
```

### Options

| Flag | Argument | Description |
|---|---|---|
| `-h`, `--help` | | Show help and exit |
| `-c`, `--config` | `<path>` | Load config JSON from `<path>` |
| `--lut` | `<name>` | Set colour map for the next volume (by display name) |
| `-G`, `--gray` | | Set Gray colour map for the next volume |
| `-H`, `--hot` | | Set Hot Metal colour map for the next volume |
| `-S`, `--spectral` | | Set Spectral colour map for the next volume |
| `-r`, `--red` | | Set Red colour map for the next volume |
| `-g`, `--green` | | Set Green colour map for the next volume |
| `-b`, `--blue` | | Set Blue colour map for the next volume |
| `--qc` | `<input.csv>` | Enable QC mode with input CSV (see below) |
| `--qc-output` | `<output.csv>` | Output CSV for QC verdicts (required with `--qc`) |

Positional arguments are treated as volume file paths (MINC2 `.mnc` files).
LUT flags apply to the next volume on the command line.

### Examples

View a single volume:

```bash
./new_register ../test_data/mni_icbm152_t1_tal_nlin_sym_09c.mnc
```

View two volumes side-by-side with different colour maps:

```bash
./new_register --gray vol1.mnc --hot vol2.mnc
```

Load a saved configuration:

```bash
./new_register --config my_config.json
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
| `path` | string | (required) | Path to the MINC2 volume file |
| `colour_map` | string | `"Gray"` | Colour map display name |
| `value_min` | double or null | auto | Display range minimum |
| `value_max` | double or null | auto | Display range maximum |
| `slice_indices` | [int, int, int] | `[-1,-1,-1]` | Initial slice positions (-1 = midpoint) |
| `zoom` | [double, double, double] | `[1,1,1]` | Per-view zoom levels (sagittal, coronal, axial) |
| `pan_u` | [double, double, double] | `[0.5,0.5,0.5]` | Per-view horizontal pan center |
| `pan_v` | [double, double, double] | `[0.5,0.5,0.5]` | Per-view vertical pan center |

All volume fields except `path` are optional; omitted fields use their defaults.

### Available Colour Maps

Use these display names in the `colour_map` JSON field and `--lut` CLI flag:

| Name | Description |
|---|---|
| `Gray` | Grayscale (black to white) |
| `Hot Metal` | Black-red-yellow-white |
| `Hot Metal (neg)` | Inverted hot metal |
| `Cold Metal` | Black-blue-cyan-white |
| `Cold Metal (neg)` | Inverted cold metal |
| `Green Metal` | Black-green-white |
| `Green Metal (neg)` | Inverted green metal |
| `Lime Metal` | Black-lime-white |
| `Lime Metal (neg)` | Inverted lime metal |
| `Red Metal` | Black-red-white |
| `Red Metal (neg)` | Inverted red metal |
| `Purple Metal` | Black-purple-white |
| `Purple Metal (neg)` | Inverted purple metal |
| `Spectral` | Rainbow spectrum |
| `Red` | Red channel only |
| `Green` | Green channel only |
| `Blue` | Blue channel only |
| `Red (neg)` | Inverted red channel |
| `Green (neg)` | Inverted green channel |
| `Blue (neg)` | Inverted blue channel |
| `Contour` | Contour lines |

## QC Mode

QC mode is a batch review workflow for rating medical imaging datasets.
Users step through rows of a CSV file, viewing the volumes for each row
side by side, and marking each column as PASS or FAIL with optional
comments. Verdicts are auto-saved to the output CSV on every change.

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
