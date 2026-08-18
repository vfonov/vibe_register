# PLAN.md

Full specification for the terminal-based medical-image slice viewer, delivered as a **subproject** at `new_register/mriv/`, inside the parent ImGui-based application. `CLAUDE.md` covers *how* to work here; this document covers *what* to build and — critically — what **not** to build because the parent already provides it.

## Project overview

A fast command-line utility for viewing 2D slices of 3D medical imaging volumes **directly in a terminal**, designed for remote use over SSH. It reads one or more volume files, extracts the requested slices, and displays them using terminal graphics protocols (Kitty, sixel, iTerm2) via [notcurses](https://github.com/dankamongmen/notcurses).

Think "`cat` for medical images, over SSH."

The tool is intentionally lightweight and non-interactive-first: a user invokes it, sees the requested slice(s), and the program exits. Minimal keystroke navigation is available when run in a TTY as a bonus, not the point.

### Why this exists

The parent application is a full-featured ImGui-based viewer for local, interactive use. It does not work well over remote connections — X11 forwarding is chatty, VNC is bandwidth-heavy, and both need a graphical session on the server. This subproject provides a terminal-only path for the "I just SSH'd into an HPC node and need to sanity-check a volume" workflow. Bandwidth is idle when idle, ~tens of KB per slice change, zero server-side GPU or display server required.

### Primary use cases

1. Quickly inspect a MINC2 (`.mnc`) or NIfTI (`.nii`, `.nii.gz`) file on a remote HPC node over SSH.
2. Pipe or script slice extraction (e.g., dump the middle axial slice of every file in a directory to check a preprocessing pipeline).
3. Eyeball a batch of outputs as a strip: `mriv out/*.mnc`.

DICOM archive browsing is **not** a v1 use case — see [Deferred work](#deferred-work--blocked-on-the-parent).

## Non-goals

- Not a replacement for the parent application's interactive analysis features.
- No 3D or volume rendering, no MPR reslicing along arbitrary oblique planes. Orthogonal slices only (axial/sagittal/coronal) in v1.
- No segmentation, annotation, or measurement tools.
- No GUI. Ever. The parent has ImGui; this subproject is deliberately the opposite.
- No new format support. Whatever formats the parent reads, we read — nothing more, nothing less.

## The reuse principle

This is the most important part of the spec. **This subproject exists to add a terminal rendering path to capabilities the parent already provides.** It is not a rewrite, not a fork, not a "clean-room" reimplementation.

### What the parent already provides (do not reimplement)

The parent is `new_register/`, the directory this subproject sits inside. Its GPU-free static library target is **`nr_core`** (`new_register/CMakeLists.txt:429`), which bundles exactly the layer this subproject needs: `Volume`, `SliceRenderer`, `ColourMap`, `Transform`, `TagWrapper`, `AppConfig`, `NiftiVolume`. `new_mincpik` is an existing headless consumer of `nr_core` and is the closest working model for what `mriv` should be.

The concrete API surface — this list replaces any need for a separate parent-API survey:

**Volume loading — `include/Volume.h`**

```cpp
void Volume::load(const std::string& filename);   // Volume.h:57, throws std::runtime_error
```

One entry point for every supported format. It dispatches on the filename: `isNiftiFile()` → `loadNiftiFile()` for `.nii` / `.nii.gz` (`include/NiftiVolume.h`), otherwise the MINC2 path via minc2-simple (`src/Volume.cpp:189`). **`mriv` never sniffs formats, never opens a file itself, and never links a format library.**

**Metadata — public members of `Volume`**

`dimensions` (`glm::ivec3`), `step` (voxel spacing, mm), `start`, `dirCos`, `voxelToWorld` / `worldToVoxel`, `min_value` / `max_value`. This is everything `--info` needs. `new_register/tests/dump_vol.cpp` is a working reference for how to format it.

**Slice rendering — `include/SliceRenderer.h`**

```cpp
struct VolumeRenderParams          // SliceRenderer.h:24
{
    double valueMin, valueMax;     // intensity range (this is the window/level model)
    ColourMapType colourMap;
    float overlayAlpha;
    int underColourMode, overColourMode;
    bool useLogTransform, invertColourMap;
};

struct RenderedSlice               // SliceRenderer.h:37
{
    std::vector<uint32_t> pixels;  // packed 0xAABBGGRR
    int width, height;
};

RenderedSlice renderSlice(const Volume& vol,
                          const VolumeRenderParams& params,
                          int viewIndex,
                          int sliceIndex);   // SliceRenderer.h:50
```

**The single most important reuse fact in this document:** `renderSlice()` returns a *finished* RGBA buffer. `0xAABBGGRR` packed into a `uint32_t` is R,G,B,A in ascending byte order on little-endian — precisely the layout `ncvisual_from_rgba()` expects. There is no window/level step to write, no colour mapping to write, and no 8-bit conversion to write. The parent hands us blittable pixels.

**Intensity range — `include/Volume.h`**

```cpp
float Volume::computeQuantile(double q) const;   // Volume.h:59
```

This is the percentile primitive behind `--auto-window`. Call it; do not write a histogram.

**Pixel aspect — `include/Volume.h`**

```cpp
double Volume::slicePixelAspect(int axisU, int axisV) const;   // Volume.h:76
```

Returns `|step[axisU]| / |step[axisV]|`. Voxels are routinely anisotropic in this domain (2–5 mm slice thickness against 1 mm in-plane is normal). **The resampler must apply this factor or every non-isotropic volume renders visibly stretched.** This is a hard requirement, not a refinement.

**Colour maps — `include/ColourMap.h`**

`enum class ColourMapType` (`ColourMap.h:13`) provides 18 maps: GrayScale, HotMetal, ColdMetal, GreenMetal, LimeMetal, RedMetal, PurpleMetal, Spectral, Red, Green, Blue, Contour, Viridis, Jet, Magma, Inferno, Plasma, Turbo. Exposed through `--colourmap`.

If the parent's version has bugs or limitations, we fix or extend it *in the parent*, then consume the improved version here. This subproject never contains a second copy of format-specific logic.

### What this subproject adds

- **A notcurses-based rendering path.** Terminal detection, pixel-protocol selection, escape-sequence output.
- **A CLI (cxxopts).** Argument parsing, file dispatch, output modes (single slice, strip of files, info-only).
- **Terminal-appropriate presentation.** Resampling the parent's RGBA slice to the terminal's cell/pixel grid *while honouring `slicePixelAspect()`*, arranging multiple slices in a strip, `--info` as plain text.
- **A minimal interactive mode.** Vim-style keys when stdout is a TTY and a single file is opened. Bonus, not the point.

That's it. Everything else comes from the parent.

### When the parent's API doesn't quite fit

Order of preference:

1. **Use it as-is** with a different call pattern. Ninety percent of the time the mismatch is imagined.
2. **Add a small adapter in this subproject** if there's a genuine impedance mismatch. Adapters live in `src/adapt/` and are as thin as possible. Note that the mismatch this directory was invented for — "the parent hands you slices tied to an ImGui texture handle" — does not exist: `renderSlice()` returns a plain `std::vector<uint32_t>`. Expect `src/adapt/` to stay nearly empty. If it grows, something is wrong.
3. **Extend the parent's API** with a minimal, backward-compatible addition. This is a parent-project change, made in a separate commit (ideally a separate PR), then consumed here.

Never fork the parent's format-handling code into this subproject. That is the failure mode this document exists to prevent.

## Requirements

### Language and standards

- **C++17.** The parent sets `CMAKE_CXX_STANDARD 17` (`new_register/CMakeLists.txt:7`); we match it. Do not reach for C++20 features. (The parent has a documented, currently-unscheduled ambition to move to C++23 — see `AGENTS.md` §9. When that lands, this subproject follows; it does not lead.)
- Prefer the standard library. No Boost.

### Dependencies

This subproject adds exactly two things on top of `nr_core`:

- **[notcurses](https://github.com/dankamongmen/notcurses)** — terminal rendering, **C API only**. Non-negotiable; do not add alternative TUI libraries. Version 3.0.7 is installed on the dev host, headers at `/usr/include/notcurses/`.
- **[cxxopts](https://github.com/jarro2783/cxxopts)** — command-line parsing. Header-only, fetched via CMake `FetchContent`.

> **Deliberate divergence — do not "fix" this.** The parent used to use cxxopts and removed it; `new_register/src/main.cpp:49` reads `// CLI argument parsing (replaces cxxopts)`, and `src/mincpik/mincpik_cli.{h,cpp}` is a hand-rolled argv walker. This subproject uses cxxopts anyway, by explicit decision. An agent that notices the inconsistency and "aligns with the parent" is undoing a deliberate choice.

No test framework is fetched — we reuse the parent's (see [Testing](#testing)).

Notably **not** dependencies of this subproject, because `nr_core` already links them transitively (`new_register/CMakeLists.txt:442`): minc2-simple, MINC2/libminc, HDF5, NetCDF, zlib, glm, nlohmann_json, Eigen, and the vendored niftilib (`nifti1_io.c`, `znzlib.c`).

If you are about to add any of those to this subproject's `CMakeLists.txt`, stop — link `nr_core` and call its API instead.

### Platforms

Whatever the parent supports: Linux and macOS. Do not extend or restrict that matrix.

## Build integration

### As a subdirectory of the parent

`new_register/CMakeLists.txt` is the build root; there is **no top-level `/app/CMakeLists.txt`**. This subproject lives at `new_register/mriv/`, a direct child of the build root, so the call is the plain form:

```cmake
option(BUILD_TERMINAL_VIEWER "Build the terminal-based slice viewer (mriv)" ON)
if(BUILD_TERMINAL_VIEWER AND NOT BUILD_QC_ONLY)
    add_subdirectory(mriv)
endif()
```

Place the option beside the existing `BUILD_QC_ONLY` / `BUILD_NEW_QC` options (`new_register/CMakeLists.txt:24-27`), and the `add_subdirectory()` after the `nr_core` target is defined (line 429). This edit is a **parent-project change** and belongs in its own commit, separate from any subproject work.

This subproject's `CMakeLists.txt`:

- Defines a single executable target, `mriv`.
- Links `nr_core` (which carries its include dirs and transitive deps as `PUBLIC`).
- Links `notcurses` (via `pkg-config` / `find_package`).
- Fetches and links `cxxopts`.
- Registers tests with `mriv_`-prefixed CTest names so they are selectable from the parent's `ctest`.

### Standalone build (optional)

A standalone build lets you iterate without rebuilding the parent's ImGui/Vulkan code:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMRIV_STANDALONE=ON
```

Standalone mode still needs a **pre-built `nr_core`** plus its transitive MINC/HDF5 stack — it removes the GUI build cost, not the parent dependency. Point it at an existing `new_register/build` tree. Standalone must not diverge behaviorally from the integrated build; it's a build convenience, not a separate product.

## Architecture

### High-level flow

```
CLI args (cxxopts)                                  ← THIS SUBPROJECT
   ↓
Volume::load(path)                                  ← PARENT (nr_core)
   ↓
VolumeRenderParams{ valueMin, valueMax, colourMap } ← built from flags, or from
                                                       Volume::computeQuantile()
   ↓
renderSlice(vol, params, viewIndex, sliceIndex)     ← PARENT (nr_core)
   ↓  RenderedSlice: RGBA pixels, ready to blit
resample to terminal cells,                         ← THIS SUBPROJECT
  scaled by Volume::slicePixelAspect()
   ↓
Terminal::blit()  — ncvisual_from_rgba/ncvisual_blit ← THIS SUBPROJECT
   ↓
escape sequences on stdout → user's terminal decodes → pixels
```

The boundary sits one step later than you might assume: the parent owns everything through colour-mapped RGBA. This subproject owns **only** resampling and notcurses.

### Components in this subproject

- **`src/cli/`** — cxxopts wiring, argument validation, dispatch to modes (`--info`, render single, render strip, interactive).
- **`src/render/`** — notcurses wrapper (`Terminal` RAII class), pixel-support detection, blit path, resampler (nearest first, bilinear later if needed).
- **`src/adapt/`** — thin adapters, if any prove necessary. Expected to stay nearly empty.
- **`src/interactive/`** — the minimal TTY-mode key handler. Skip until the non-interactive path is fully green.
- **`src/main.cpp`** — the entry point. Should be short; most logic lives in `cli/` and `render/`.

### Components NOT in this subproject

- Anything that reads MINC2 or NIfTI. Call `Volume::load()`.
- Anything that extracts a slice, applies an intensity range, or maps colour. Call `renderSlice()`.
- Anything that computes intensity percentiles. Call `Volume::computeQuantile()`.

### Rendering layer notes

Thin wrapper around the notcurses **C API** in `src/render/terminal.{hpp,cpp}`:

- Initialize with `notcurses_init()`; RAII-wrap in a `Terminal` class so `notcurses_stop()` runs on destruction.
- Call `notcurses_check_pixel_support()` to confirm image capability.
- On no pixel support: emit a clear message to `std::cerr` suggesting Kitty, Ghostty, WezTerm, iTerm2, or Konsole, and either fall back to Unicode-block rendering (`NCBLIT_2x1` / `NCBLIT_2x2`) or exit non-zero, per `--require-pixels`.
- Do NOT hardcode a pixel protocol. Let notcurses auto-detect. Respect the user's `NCPIXEL_IMPL` override.
- Feed the resampled RGBA buffer straight to `ncvisual_from_rgba()` → `ncvisual_blit()` with `NCBLIT_PIXEL`. No pixel-format conversion is needed; see the note on `0xAABBGGRR` above.

Keep this layer format-agnostic: it takes an RGBA buffer and terminal-region dimensions, nothing more. See `CLAUDE.md` for why we avoid the `ncpp::` C++ bindings.

### Axis convention

The parent's `renderSlice()` takes a `viewIndex` whose ordering is **not** the intuitive x/y/z (`SliceRenderer.h:47`):

| `--axis` | Plane | Parent `viewIndex` | Slices along |
|----------|-------|--------------------|--------------|
| `z`      | axial (default) | `0` | Z, `dimensions.z` slices |
| `x`      | sagittal | `1` | X, `dimensions.x` slices |
| `y`      | coronal  | `2` | Y, `dimensions.y` slices |

Do the mapping once, in the CLI layer, and keep parent-facing code speaking `viewIndex`.

### CLI surface (cxxopts)

Single executable, scriptable first, interactive second:

```
mriv [options] <file>...

Positional:
  file                    One or more MINC2 (.mnc) or NIfTI (.nii/.nii.gz)
                          volumes, each opened via Volume::load().

Slice selection:
  -v, --views <list>      Planes to show, stacked top to bottom: a comma-
                          separated subset of x,y,z (default: z,x,y -- all
                          three).
  -a, --axis <x|y|z>      The axis --slice positions and the keyboard moves
                          (default: z / axial).
  -s, --slice <n|p%|mid>  Slice index, percentage, or "mid" (default: mid).

Display:
  -R, --range <low,hi>    Intensity range for mapping: low maps to the
                          darkest colour, high to the brightest.
      --auto-window       Percentile-based auto range (default on).
  -c, --colourmap <name>  Colour map name, or one name per file separated by
                          commas (default: Spectral for the first volume,
                          Gray for the rest). See ColourMap.h.
      --invert            Invert the colour map.
      --require-pixels    Exit non-zero if the terminal has no pixel protocol.
      --max-width <px>    Cap the rendered image width in pixels.
      --scale <n>         Integer pixel magnification factor (default: 1).

Interactive:
      --interactive       Navigate with the keyboard. Default when stdout is
                          a terminal.
      --no-interactive    Print one slice and exit, even on a terminal.

Info:
  -i, --info              Print volume metadata and exit (no rendering).
  -h, --help              Show help.
      --version           Show version.
```

`-R`/`--range` maps directly onto the parent's range model — there is no window/level
arithmetic in between:

```cpp
params.valueMin = rangeLow;
params.valueMax = rangeHigh;
```

`--auto-window` (the default) fills the same two fields from `Volume::computeQuantile()`
instead; it is an error to combine the two.

`--scale` magnifies the resampled display image by an integer factor via nearest-neighbour
pixel replication (each display pixel becomes an `n x n` block), applied *after* the
aspect-correct fit into the terminal's pixel box — see `render/Resample.hpp`'s
`resampleToDisplay()`. A `--scale` of 1 (the default) is a no-op.

### The display grid

Both the one-shot path and the interactive loop draw the same thing: a grid, composed into a single
RGBA buffer and blitted once.

```
        volume 0        volume 1        <- columns, one per input file, in argument order
      +-------------+-------------+
 z    |   axial     |   axial     |     <- rows, one per --views entry, in the order given
      +-------------+-------------+
 x    |  sagittal   |  sagittal   |
      +-------------+-------------+
 y    |  coronal    |  coronal    |
      +-------------+-------------+
```

Judging a registration or a conversion means seeing all three planes at once, and comparing two
volumes means seeing them side by side with the same slice under the cursor — so all three planes
are shown by default, and `--views` narrows the set.

One bitmap for the whole grid is load-bearing rather than incidental: it keeps the notcurses
wrappers free of per-pane plane bookkeeping, makes the layout a pure function testable with no
terminal at all, and makes redrawing — or re-blitting the final frame on exit — a matter of handing
over a buffer that already exists. `render/Layout.hpp` divides the box, `render/FrameBuilder.hpp`
renders the cells, `render/Compose.hpp` composites them.

The terminal's pixel box is a *budget*, not the output size. Each slice is fitted into its share of
it and the frame is then sized to what those fits produced, because `renderSliceForDisplay()` never
upscales: a box-sized canvas would wrap a small volume in a screenful of black and blit megabytes of
it on every keypress. `--scale` magnifies the gutter along with the panes so the proportions hold.

Multiple inputs become columns, captioned once with the file names in order so they can be told
apart; a single-file invocation is left uncaptioned, staying pure image bytes for the "cat for
medical images" case. A file that fails to load is reported and skipped; the run only exits non-zero
if nothing rendered at all.

### Interactive mode

`--interactive`, or auto-detected whenever stdout is a TTY. The file count does not enter into it:
several volumes become columns of one navigable grid, which is more use interactively than piped.

| Key | Effect |
|-----|--------|
| `j` / `k` | Move the cursor ±1 along the active axis — every column moves together |
| `x` / `y` / `z` | Choose the axis `j`/`k` moves. Does **not** change which views are shown |
| `Tab`, `1`–`9` | Select the active column |
| `c` / `C` | Cycle the active column's colour map forward / back |
| `r` | Open the range prompt for the active column |
| `q`, `Esc` | Quit |
| *in prompt* | printable → append, Backspace → delete, Enter → apply, `Esc` → cancel |

There is no `h`/`l` time navigation — see [Deferred work](#deferred-work--blocked-on-the-parent).

Position is a single 3D voxel cursor, held in the *first* volume's index space and mapped onto the
others by `mapSliceIndex()`. One cursor is what makes navigation synchronised, which is the point of
showing columns side by side; equal slice counts map index-for-index, and mismatched ones track
proportionally. Each axis keeps its own component, so leaving an axis and returning to it lands
exactly where you left off — the three are geometrically independent and there is no meaningful way
to carry a position between them.

Ranges are **typed, not scaled**: `r` prompts for `low high` (or `low,high`) on the status row, for
the active column only. On real data the numbers you want are known, and hunting for them by
repeated multiplication was slower than typing them. An entry that will not parse keeps the prompt
and the typing and says why. While the prompt is open every key belongs to it, including the
navigation letters and `Esc` — typing a number must not also move a slice.

Colour maps default to Spectral for the first volume and grayscale for the rest: the first is
usually the one being judged and the others are references it is compared against. `--colourmap`
overrides, with one name for all of them or one per file.

`--no-interactive` is the escape hatch for a user sitting at a terminal who wants the one-shot
behaviour anyway; scripts can pass it unconditionally. An explicit `--interactive` that cannot be
honoured — no TTY, or combined with `--info` — is refused with a reason and a non-zero exit rather
than silently degrading to a one-shot render, which would look to the user like the keys were broken.

Interactive mode uses **full notcurses** (alternate screen, `notcurses_render()` per frame), not the
`ncdirect` path the one-shot renderer uses. Redrawing in place requires releasing the previous
frame's bitmap before drawing the next, which is what notcurses's sprixel lifecycle handles; the
teardown bitmap-clearing that made `ncdirect` necessary for the one-shot path is not a problem on
the alternate screen, where restoring the terminal on exit is the wanted behaviour.

Quitting does leave the last frame on the terminal, though. The alternate screen takes its contents
with it, so the final frame is re-emitted through the one-shot `ncdirect` path — the same code that
prints an image and exits, and the only one proven not to have its bitmaps wiped on teardown. It is
captioned with the status row minus the key legend.

## Project layout

```
new_register/mriv/               # this subproject, a child of the parent's build root
├── CMakeLists.txt
├── CLAUDE.md
├── PLAN.md
├── README.md
├── cmake/                       # any subproject-specific find modules
├── src/
│   ├── main.cpp                 # entry point
│   ├── cli/
│   │   └── options.{hpp,cpp}    # cxxopts wiring
│   ├── render/
│   │   ├── terminal.{hpp,cpp}   # notcurses wrapper (C API)
│   │   └── resample.{hpp,cpp}   # slice-to-display-size resampling
│   ├── adapt/                   # thin adapters, if any prove necessary
│   ├── interactive/
│   │   └── keys.{hpp,cpp}       # vim-key mode; comes last
│   └── util/
│       └── log.hpp
└── tests/
    └── data/                    # small sample volumes (< 1 MB each)
```

Larger reference volumes already exist in `test_data/` at the repository root — the parent's tests use them (see `TEST_DATA_DIR` in `new_register/tests/CMakeLists.txt`). Reuse those for smoke testing rather than committing new ones.

## Development environment

### Ubuntu

Only what this subproject adds; the parent's dependencies are assumed installed.

```bash
sudo apt install -y libnotcurses-dev
```

Install `libnotcurses-dev`, **not** `libnotcurses++-dev` — we use the C API only, and the C++ bindings package exists solely to provide `ncpp::`, which is banned here. `cxxopts` is fetched via CMake.

For visual testing on the dev host, a pixel-capable terminal: `kitty`, `wezterm`, or Ghostty.

### macOS (Homebrew)

```bash
brew install notcurses
```

For visual testing: iTerm2, Kitty, Ghostty, or WezTerm. **Do not rely on the built-in Terminal.app** — it supports none of the pixel protocols.

### Sanity check

```bash
pkg-config --modversion notcurses    # 3.0.7 on the current dev host
```

## Style and formatting

Follow the parent's conventions (`new_register/AGENTS.md` §6):

| Aspect | Rule |
|--------|------|
| Standard | C++17 |
| Types / classes | `PascalCase` |
| Methods / variables | `camelCase` |
| Braces | Allman (opening brace on its own line) |
| Indentation | 4 spaces, no tabs |
| Error output | `std::cerr` — never `printf` to stderr |
| Includes | system → library → local |

Namespace this subproject under `mriv::term`. No clash: the parent uses only one namespace, `QC::`, for `src/qc/`.

## Testing

The parent has **no** doctest, Catch2, or GoogleTest — and we do not add one. Its 26 tests are plain `assert` + `main()` executables registered through a small macro in `new_register/tests/CMakeLists.txt`:

```cmake
add_nr_test(<name> SOURCES ... INCLUDES ... LINKS nr_core)   # builds tests/<name>.cpp
add_test(NAME <CTestName> COMMAND <name> <args>)             # registers with CTest
```

`add_nr_test()` (`tests/CMakeLists.txt:25`) builds the executable; the separate `add_test()` gives it its CTest name. **Prefix the CTest names `mriv_`** so `ctest -R "^mriv_"` selects this subproject's suite.

- **Prefer testing this subproject's logic** — CLI parsing, resampling math (including the `slicePixelAspect()` correction), terminal wrapping, axis→`viewIndex` mapping. Do not re-test `Volume::load()` or `renderSlice()`; the parent already does.
- Where parent APIs are expensive in tests, a synthetic `Volume` is cheap: `Volume::generate_test_data()` (`Volume.h:67`) exists for exactly this, and `tests/sq1.mnc` / `sq2.mnc` are tiny real volumes.
- Integration tests exercise the full path from CLI invocation to encoded output. Capture the escape-sequence output to a buffer and assert on structure, not exact bytes.
- Render tests requiring a real terminal are gated on `MRIV_TEST_RENDER=1` and skipped by default.
- **No absolute paths in test source** (`AGENTS.md` §7.9) — pass data paths as arguments from CMake using `CMAKE_CURRENT_SOURCE_DIR` / `TEST_DATA_DIR`.

## Things Claude should NOT do

- Do not reimplement volume reading, slice extraction, intensity ranging, colour mapping, or metadata parsing. `nr_core` has all of these.
- Do not add niftilib, DCMTK, GDCM, MINC, HDF5, or any format library to this subproject's link list. Link `nr_core`.
- Do not replace cxxopts with a hand-rolled parser to "match the parent." That divergence is intentional and documented above.
- Do not add doctest, Catch2, or GoogleTest. Use `add_nr_test()` and plain `assert`.
- Do not modify parent-project source from commits in this subproject unless the task is explicitly to extend a parent API — and even then, do it in a separate commit.
- Do not add a GUI. Not Qt, not ImGui, not "just a preview."
- Do not add Boost or other header-heavy libraries.
- Do not swap notcurses for FTXUI, ncurses, or hand-rolled escape sequences.
- Do not use the `ncpp::` C++ bindings. Use the C API directly.
- Do not build `--series`, `--list-series`, or `--time`. Nothing in the parent backs them; see below.
- Do not implement caching, prefetching, or interactivity before the non-interactive path works end-to-end.
- Do not skip the `slicePixelAspect()` correction "for now." Anisotropic volumes are the common case, not the exception.
- Do not paper over missing terminal image support with ASCII art unless explicitly asked. A clear error message beats a bad approximation.

## Deferred work — blocked on the parent

These features appear in earlier drafts of this document. They are **not** cancelled, but they cannot be built here, because there is no parent API to call. Each needs a parent-side capability to land first.

### DICOM series handling (`--series`, `--list-series`)

**Status: blocked.** `grep -ri dicom new_register/src new_register/include` returns nothing. The parent reads MINC2 and NIfTI only — there is no DICOM reader, no series scanner, no `SeriesInstanceUID` grouping, and no directory-aware reader of any kind.

**Unblocked when:** the parent gains DICOM support behind `Volume::load()` (or a sibling directory-scanning API). At that point `mriv` adds `--series` / `--list-series` as a thin CLI layer over it. Until then, positional arguments are files only, and passing a directory should be a clean error.

Do not add DCMTK or GDCM to this subproject to work around this. That is precisely the fork this document exists to prevent.

### 4D volumes and time navigation (`-t` / `--time`)

**Status: blocked.** `Volume::dimensions` is a `glm::ivec3` (`Volume.h:21`) — the parent's volume model is strictly three-dimensional. The MINC2 path indexes only `MINC2_DIM_X/Y/Z` (`src/Volume.cpp:213-215`) and the NIfTI path reads only `nx, ny, nz` (`src/NiftiVolume.cpp:38`). A 4th dimension is silently dropped by both readers.

**Unblocked when:** the parent's `Volume` grows a time/vector dimension and `renderSlice()` accepts a time index. Until then there is no `-t` flag and no `h`/`l` interactive keys.

## Milestones

Not deadlines — a suggested order of operations so the subproject is always in a runnable state within the parent's build.

1. **M1 — Skeleton and parent integration.** `mriv/CMakeLists.txt` builds an empty `mriv` binary that prints `--help` via cxxopts and links `nr_core` + notcurses. Parent-side `add_subdirectory()` lands as its own commit. One trivial `mriv_`-prefixed test passes under the parent's `ctest`.
2. **M2 — Non-interactive render path.** `Terminal` wrapper around the notcurses C API, aspect-correct resampler, wired to `Volume::load()` + `renderSlice()`. `mriv file.mnc` displays the middle axial slice. Manual smoke test on Kitty against the volumes in `test_data/`.
3. **M3 — `--info` mode** (modelled on `tests/dump_vol.cpp`), `--axis` / `--slice` selection, `--window` / `--level` / `--auto-window`, `--colourmap`.
4. **M4 — Multi-file strip rendering**, `--max-width`, `--require-pixels` and the no-pixel-support fallback path.
5. **M5 — Interactive TTY mode** (vim keys, minus time navigation).
6. **M6 — macOS build verification, standalone-build support, README, docs polish.**

Each milestone lands with tests. Each milestone leaves both the standalone binary and the parent's build runnable.
