# PLAN.md

Design and scope of the terminal-based medical-image slice viewer, delivered as a **subproject** at `new_register/mriv/`, inside the parent ImGui-based application. `CLAUDE.md` covers *how* to work here; this document covers *what* it does and — critically — what **not** to build because the parent already provides it. Milestones M1–M5 are implemented; M6 is partly done (see [Milestones](#milestones)). `PLAN_TESTING.md` covers the test strategy; `HANDOFF.md` records the implementation traps referenced below.

## Project overview

A fast command-line utility for viewing 2D slices of 3D medical imaging volumes **directly in a terminal**, designed for remote use over SSH. It reads one or more volume files, extracts the requested slices, and displays them using terminal graphics protocols (Kitty, sixel, iTerm2) via [notcurses](https://github.com/dankamongmen/notcurses).

Think "`cat` for medical images, over SSH."

The tool is intentionally lightweight and scriptable: piped, or with `--no-interactive`, it prints the requested slice(s) and exits. When stdout is a terminal it opens a keyboard-navigable view instead (see [Interactive mode](#interactive-mode)).

### Why this exists

The parent application is a full-featured ImGui-based viewer for local, interactive use. It does not work well over remote connections — X11 forwarding is chatty, VNC is bandwidth-heavy, and both need a graphical session on the server. This subproject provides a terminal-only path for the "I just SSH'd into an HPC node and need to sanity-check a volume" workflow. Bandwidth is idle when idle, ~tens of KB per slice change, zero server-side GPU or display server required.

### Primary use cases

1. Quickly inspect a MINC2 (`.mnc`) or NIfTI (`.nii`, `.nii.gz`) file on a remote HPC node over SSH.
2. Pipe or script slice extraction (e.g., dump the middle axial slice of every file in a directory to check a preprocessing pipeline).
3. Eyeball a batch of outputs side by side, one column per file: `mriv out/*.mnc`.

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

The parent is `new_register/`, the directory this subproject sits inside. Its GPU-free static library target is **`nr_core`** (`add_library(nr_core …)` in `new_register/CMakeLists.txt`), which bundles exactly the layer this subproject needs: `Volume`, `VolumeReorient`, `SliceRenderer`, `ColourMap`, `Transform`, `TagWrapper`, `AppConfig`, `NiftiVolume` (plus the parent's own `CliArgs`, which mriv does not use). `new_mincpik` is an existing headless consumer of `nr_core` and is the closest working model for what `mriv` should be.

The concrete API surface — this list replaces any need for a separate parent-API survey:

**Volume loading — `include/Volume.h`**

```cpp
void Volume::load(const std::string& filename);   // throws std::runtime_error
```

One entry point for every supported format. It dispatches on the filename: `isNiftiFile()` → `loadNiftiFile()` for `.nii` / `.nii.gz` (`include/NiftiVolume.h`), otherwise the MINC2 path via minc2-simple (`Volume::load()` in `src/Volume.cpp`). **`mriv` never sniffs formats, never opens a file itself, and never links a format library.**

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
float Volume::computeQuantile(double q) const;   // Volume.h:53
std::vector<float> Volume::computeQuantiles(const std::vector<double>& qs) const;   // Volume.h:60
```

This is the percentile primitive behind `--auto-window`. Call it; do not write a histogram. When you need more than one quantile (e.g. low and high window bounds), use `computeQuantiles()` — it copies the volume once instead of once per quantile.

**Pixel aspect — `include/Volume.h`**

```cpp
double Volume::slicePixelAspect(int axisU, int axisV) const;
```

Returns `|step[axisU]| / |step[axisV]|`. Voxels are routinely anisotropic in this domain (2–5 mm slice thickness against 1 mm in-plane is normal). **The resampler must apply this factor or every non-isotropic volume renders visibly stretched.** This is a hard requirement, not a refinement.

**Colour maps — `include/ColourMap.h`**

`enum class ColourMapType` (`ColourMap.h:13`) provides 18 maps: GrayScale, HotMetal, ColdMetal, GreenMetal, LimeMetal, RedMetal, PurpleMetal, Spectral, Red, Green, Blue, Contour, Viridis, Jet, Magma, Inferno, Plasma, Turbo. Exposed through `--colourmap`.

If the parent's version has bugs or limitations, we fix or extend it *in the parent*, then consume the improved version here. This subproject never contains a second copy of format-specific logic.

### What this subproject adds

- **A notcurses-based rendering path.** Terminal detection, pixel-protocol selection, escape-sequence output.
- **A CLI (cxxopts).** Argument parsing, file dispatch, output modes (one-shot render, interactive, info-only).
- **Terminal-appropriate presentation.** Resampling the parent's RGBA slice into the terminal's pixel box *while honouring `slicePixelAspect()`*, arranging views and files in a grid, `--info` as plain text.
- **An interactive mode.** Keyboard navigation, the default when stdout is a TTY, for any number of files.

That's it. Everything else comes from the parent.

### When the parent's API doesn't quite fit

Order of preference:

1. **Use it as-is** with a different call pattern. Ninety percent of the time the mismatch is imagined.
2. **Add a small adapter in this subproject** if there's a genuine impedance mismatch. Keep it as thin as possible, beside the code that needs it. The mismatch a separate adapter layer was originally planned for — "the parent hands you slices tied to an ImGui texture handle" — does not exist: `renderSlice()` returns a plain `std::vector<uint32_t>`, which `render/SlicePipeline.cpp` consumes directly. No `src/adapt/` directory has been needed; if adapter code starts to grow, something is wrong.
3. **Extend the parent's API** with a minimal, backward-compatible addition. This is a parent-project change, made in a separate commit (ideally a separate PR), then consumed here.

Never fork the parent's format-handling code into this subproject. That is the failure mode this document exists to prevent.

## Requirements

### Language and standards

- **C++17.** The parent sets `CMAKE_CXX_STANDARD 17` (`new_register/CMakeLists.txt:7`); we match it. Do not reach for C++20 features. (The parent has a documented, currently-unscheduled ambition to move to C++23 — see the "C++23 Modernization" section of the root `PLAN.md`. When that lands, this subproject follows; it does not lead.)
- Prefer the standard library. No Boost.

### Dependencies

This subproject adds exactly two things on top of `nr_core`:

- **[notcurses](https://github.com/dankamongmen/notcurses)** — terminal rendering, **C API only**. Non-negotiable; do not add alternative TUI libraries. Version 3.0.7 is installed on the dev host, headers at `/usr/include/notcurses/`. By default this is a system package, located with `find_path`/`find_library` (not `pkg-config`) and wrapped in the `mriv_notcurses` interface target. For hosts with no system notcurses (or to produce a fully self-contained static binary), `-DMRIV_BUILD_NOTCURSES=ON` downloads, builds, and statically links notcurses from source instead, via `ExternalProject_Add` — see `cmake/BuildNotcurses.cmake` and the "From-source notcurses" subsection below.
- **[cxxopts](https://github.com/jarro2783/cxxopts)** — command-line parsing. Header-only, fetched via CMake `FetchContent` (pinned to `v3.3.1`).

> **Deliberate divergence — do not "fix" this.** The parent used to use cxxopts and removed it; `new_register/src/CliArgs.cpp` opens with `// CLI argument parsing (replaces cxxopts)`, and both it and `src/mincpik/mincpik_cli.{h,cpp}` are hand-rolled argv walkers. This subproject uses cxxopts anyway, by explicit decision. An agent that notices the inconsistency and "aligns with the parent" is undoing a deliberate choice.

No test framework is fetched here — we reuse the parent's Catch2 (see [Testing](#testing)).

Notably **not** dependencies of this subproject, because `nr_core` already links them transitively (`target_link_libraries(nr_core …)` in `new_register/CMakeLists.txt`): minc2-simple, MINC2/libminc, HDF5, NetCDF, zlib, glm, nlohmann_json, Eigen, and the vendored niftilib (`nifti1_io.c`, `znzlib.c`).

If you are about to add any of those to this subproject's `CMakeLists.txt`, stop — link `nr_core` and call its API instead.

### Platforms

Whatever the parent supports: Linux and macOS. Do not extend or restrict that matrix. Linux is built and tested. On macOS the self-built static binary (`MRIV_BUILD_NOTCURSES=ON`) is verified; the rest of macOS verification is still open (M6). The parent defaults `ENABLE_TESTS` to `OFF` on macOS (its headless UI tests need OSMesa), and `mriv/tests/` is gated on the same option, so the `mriv_*` suites are not built there by default.

## Build integration

### As a subdirectory of the parent

`new_register/CMakeLists.txt` is the build root; there is **no top-level `/app/CMakeLists.txt`**. This subproject lives at `new_register/mriv/`, a direct child of the build root, and is hooked in at the end of the parent's CMakeLists (`new_register/CMakeLists.txt`). `BUILD_TERMINAL_VIEWER` is declared with the parent's other per-tool options (`BUILD_NEW_REGISTER`, `BUILD_NEW_QC`, `BUILD_NEW_MINCPIK`) near the top of that file, and enabling it makes the parent resolve the MINC stack for `nr_core`:

```cmake
if(BUILD_TERMINAL_VIEWER)
    add_subdirectory(mriv)
endif()
```

It sits last in the file because it needs the `nr_core` target and, for the tests, the `add_nr_test()` macro defined by `add_subdirectory(tests)`. It landed as its own parent-project commit.

This subproject's `CMakeLists.txt`:

- Builds `mriv_lib`, a static library of everything under `src/` except `main.cpp`, and a thin `mriv` executable that links it. Tests link `mriv_lib` too.
- `mriv_lib` links `nr_core` and `cxxopts::cxxopts` `PUBLIC` (`nr_core` carries its include dirs and transitive deps), and notcurses `PRIVATE` through the `mriv_notcurses` interface target — a system copy by default, or a self-built static one (see below).
- Fetches `cxxopts` via `FetchContent`.
- Under `if(ENABLE_TESTS)`, adds `tests/`, which registers `mriv_`-prefixed CTest names so they are selectable from the parent's `ctest`.

### From-source notcurses (`-DMRIV_BUILD_NOTCURSES=ON`)

Opt-in alternative to the default system-package (`find_path`/`find_library`) notcurses lookup, for hosts
with no system-provided notcurses, or to get a fully self-contained `mriv` binary regardless.
Implemented in `cmake/BuildNotcurses.cmake`, included from `mriv/CMakeLists.txt` only when the
option is set; the default configure never touches it.

- Downloads and builds notcurses (`v3.0.17`, pinned) and libunistring (`1.4.2`, pinned,
  sha256-verified tarball) from source via `ExternalProject_Add`, both statically linked.
  libunistring is built as its own contained `ExternalProject` (GNU autotools, static-only,
  `--with-pic`) rather than required as a system package — unlike zlib, it's a hard,
  non-optional notcurses build dependency with no `USE_UNISTRING=OFF` escape hatch upstream,
  and is frequently absent even on otherwise well-provisioned hosts.
- tinfo is built from source too (ncurses `--with-termlib`, static), and notcurses' vendored
  `CMakeLists.txt` is patched (`cmake/patch_notcurses_tinfo.cmake`) to consume it directly —
  upstream otherwise requires `pkg-config` just to locate tinfo, and macOS ships neither a
  `tinfo.pc` nor a `pkg-config` executable. This path needs no `pkg-config` at all.
- notcurses is configured with every optional feature off (`USE_MULTIMEDIA=none`,
  `USE_CXX=OFF`, `USE_DEFLATE=OFF`, `USE_QRCODEGEN=OFF`, `USE_GPM=OFF`, docs/tests/executables
  off) — mriv only uses the core C API (`ncdirect_*` for one-shot output, full `notcurses_*` for
  interactive mode, `ncvisual_from_rgba()` for both) and never decodes media files, so this build needs neither
  ffmpeg, OpenImageIO, qrcodegen, nor pandoc/doctest.
- Both notcurses static archives are linked: `notcurses-core-static` (the bulk of the C API,
  `src/lib/*.c`) **and** `notcurses-static` (`src/media/*.c`) — `notcurses_init()` and
  `ncdirect_init()` themselves are defined in `src/media/shim.c`, which upstream's CMakeLists
  glob places in the *multimedia* target, not core. With `USE_MULTIMEDIA=none` this only pulls
  in a harmless stub backend, not real media-decoding code.
- Base-OS runtime libraries (`libm`, `librt`) stay dynamic, same as any other Linux
  binary — notcurses, libunistring and tinfo are statically linked. The parent project globally
  prefers `.a` over `.so` in `find_library()` resolution (for libminc's own deps); this file
  overrides that back to `.so`-first in its own directory scope before resolving those three,
  before resolving those, to avoid accidentally linking glibc's static `libm.a`/`librt.a` (which pulls in surprising
  extra undefined symbols and produces `DT_TEXTREL` in a PIE binary).
- `-DBUILD_TERMINAL_VIEWER=OFF` (the pre-existing option gating `add_subdirectory(mriv)` in the
  parent's `CMakeLists.txt`) skips this subproject — and therefore notcurses, cxxopts,
  libunistring and tinfo — entirely; the rest of `new_register` builds and tests with zero exposure to
  any of mriv's dependencies either way.

### Standalone build (planned — M6)

**Not implemented yet:** no `MRIV_STANDALONE` option exists in any CMake file. The intent is a build that lets you iterate without rebuilding the parent's ImGui/Vulkan code:

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
resample into the terminal's pixel box,             ← THIS SUBPROJECT
  scaled by Volume::slicePixelAspect(),
  composed into one grid bitmap
   ↓
Terminal::blit()    (one-shot, ncdirect)            ← THIS SUBPROJECT
Screen::drawFrame() (interactive, full notcurses)
   ↓
escape sequences on stdout → user's terminal decodes → pixels
```

The boundary sits one step later than you might assume: the parent owns everything through colour-mapped RGBA. This subproject owns **only** resampling, grid layout and notcurses (plus the CLI and the interactive loop driving them).

### Components in this subproject

- **`src/cli/`** — cxxopts wiring (`Options`), argument parsers (`SliceSelection`, `ViewList`, `ColourMapArg`), `--info` formatting (`VolumeInfo`), the interactive-or-not decision (`InteractiveDecision`), and dispatch to the modes (`Run`).
- **`src/render/`** — the one-shot ncdirect wrapper (`Terminal`) and the escape-sequence encoder its test mode uses (`Encode`, `PixelProtocol`); `--axis` → `viewIndex` mapping (`SliceGeometry`); the slice render + aspect-correct resample shared by both modes (`SlicePipeline`, `Resample`); grid layout and composition (`Layout`, `FrameBuilder`, `Compose`); the crosshair mark (`Crosshair`); PNG screenshots (`Screenshot`).
- **`src/interactive/`** — the full-notcurses wrapper (`Screen`), cursor and key handling (`ViewState`), the session loop (`Session`), state → frame request (`FramePlan`), the range prompt (`RangeEditor`), the header/status/hotkey rows (`StatusLine`) and marker placement (`Overlay`).
- **`src/util/`** — `Base64`, for the Kitty graphics payload.
- **`src/main.cpp`** — the entry point. Short; logic lives in `cli/`, `render/` and `interactive/`.

### Components NOT in this subproject

- Anything that reads MINC2 or NIfTI. Call `Volume::load()`.
- Anything that extracts a slice, applies an intensity range, or maps colour. Call `renderSlice()`.
- Anything that computes intensity percentiles. Call `Volume::computeQuantile()`.

### Rendering layer notes

Two thin RAII wrappers around the notcurses **C API**: `src/render/Terminal.{hpp,cpp}` for the one-shot path and `src/interactive/Screen.{hpp,cpp}` for interactive mode (see [Interactive mode](#interactive-mode) for why they differ). Neither header includes notcurses.

- `Terminal` uses direct mode (`ncdirect_init()` / `ncdirect_stop()`), so the printed image survives exit; `Screen` uses full notcurses (`notcurses_init()` / `notcurses_stop()`).
- Confirm image capability with `ncdirect_check_pixel_support()` / `notcurses_check_pixel_support()` respectively.
- On no pixel support: emit a clear message to `std::cerr` suggesting Kitty, Ghostty, WezTerm, iTerm2, or Konsole. Interactive mode always exits non-zero (there is no way to run a session without pixels). One-shot mode exits `0` with nothing drawn -- it is not an error for `mriv a.mnc > out.png` to be piped somewhere without pixel support -- unless the debug-only `MRIV_REQUIRE_PIXELS` env var is set, which forces a non-zero exit for scripts/CI that want a hard failure.
- Do NOT hardcode a pixel protocol in production. Let notcurses auto-detect. (`PixelProtocol` forcing exists only for the `MRIV_TEST_RENDER` test seam.)
- Feed the composed RGBA buffer straight to `ncvisual_from_rgba()` with `NCBLIT_PIXEL` — then `ncdirectf_render()` + `ncdirect_raster_frame()` in `Terminal`, `ncvisual_blit()` in `Screen`. No pixel-format conversion is needed; see the note on `0xAABBGGRR` above.

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
  -a, --axis <x|y|z>      The axis the bare form of --slice positions, and
                          the keyboard moves (default: z / axial).
  -s, --slice <spec>      Slice index, percentage, or "mid" for the --axis
                          plane (default: mid); or a per-axis list
                          "x=<n|p%|mid>,y=...,z=..." (any subset, any
                          order) to position more than one plane at once,
                          independent of --axis.

Display:
  -R, --range <low,hi>    Intensity range for mapping: low maps to the
                          darkest colour, high to the brightest.
      --auto-window       Percentile-based auto range (default on).
  -c, --colourmap <name>  Colour map name, or one name per file separated by
                          commas (default: Spectral for the first volume,
                          Gray for the rest). See ColourMap.h.
      --invert            Invert the colour map.
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
| `↑` / `↓` | Step the active row through the displayed views, wrapping — an alternative to `x`/`y`/`z` for whatever `--views` put on screen |
| `Tab`, `1`–`9`, `←` / `→` | Select the active column, wrapping |
| `c` / `C` | Cycle the active column's colour map forward / back |
| `r` | Open the range prompt for the active column |
| `s` | Save the frame currently on screen as a PNG (`screenshotNNNNNN.png`, same naming as `new_register`'s `P`) |
| `q`, `Esc` | Quit |
| `Ctrl-L` | Redraw — also fires automatically on a terminal resize |
| *in prompt* | printable → append, Backspace → delete, Enter → apply, `Esc` → cancel (arrows and letters are swallowed by the prompt, not acted on) |

There is no `h`/`l` time navigation — see [Deferred work](#deferred-work--blocked-on-the-parent).

A terminal resize is carried through the same key path as everything else: `Screen::readKey()`
translates notcurses' `NCKEY_RESIZE` into the same control code as `Ctrl-L`, and `ViewState::handleKey()`
reports it as a change without touching any state, so the caller redraws the current view against a
freshly queried display box. The display box itself is queried fresh on every frame rather than once
before the session loop, for the same reason — a box captured before a resize would leave the image
sized for a screen that no longer exists.

That "fresh query" has to force its own sync: notcurses only resizes the standard plane to match the
terminal during the *next* render or refresh, never at the moment `NCKEY_RESIZE` is read, so
`Screen::pixelGeometry()` calls `notcurses_render()` on itself before reading plane dimensions rather
than trusting them to already be current (`notcurses_refresh()` is deliberately not used here — its own
man page says it clears the screen first, which deletes placed Kitty-protocol images; see
`mriv/HANDOFF.md`).

Position is a single 3D voxel cursor, held in the *first* volume's index space and mapped onto the
others by `mapSliceIndex()`. One cursor is what makes navigation synchronised, which is the point of
showing columns side by side; equal slice counts map index-for-index, and mismatched ones track
proportionally. Each axis keeps its own component, so leaving an axis and returning to it lands
exactly where you left off — the three are geometrically independent and there is no meaningful way
to carry a position between them.

`--slice` sets that cursor's starting position, in one of two shapes `cli/SliceSelection.hpp`'s
`parseSliceSpec()` tells apart: the bare "n"/"p%"/"mid" form positions whichever single axis
`--axis` names (`Run.cpp`'s `resolveStartCursor()` resolving it no differently than before this
form existed), while "x=...,y=...,z=..." positions each axis named directly, independent of
`--axis` and of one another — the same three-independent-components model `mapSliceIndex()`
already assumes, just set from the command line instead of by navigating there. The two shapes
are mutually exclusive within one `--slice`; mixing them (e.g. "10,x=20") is a parse error.

The interactive loop marks that cursor's position inside every pane, the terminal-side analog of
`new_register`'s `ImDrawList` crosshair overlay (`Interface.cpp`): a translucent yellow one-pixel
column and row, no gap at the intersection, baked directly into each cell's `ResampledImage` before
`composeGrid()` runs rather than drawn on top of a texture. `ViewState::crosshairFor(volume,
viewIndex)` gives the cursor's two in-plane voxel coordinates for that pane, mirroring how
`sliceIndexFor()` already maps the out-of-plane one — identical for volume 0, proportional via
`mapSliceIndex()` for the rest. `render/Crosshair.hpp`'s `drawCrosshair()` places the mark with
`mapNativeToDisplay()` (`render/Resample.hpp`), the inverse of `resamplePixelsNearest()`'s
centre-of-source-pixel sampling, so it lands on the same display pixel the resampled slice itself
came from. `planFrame()` only fills `FramePane::crosshairs` when its `showCrosshair` parameter is
true; the one-shot path leaves it at the default `false` so scripted, piped output stays exactly the
clean image it always was, and only the interactive call site in `cli/Run.cpp` opts in.

The loaded volumes' names are printed above the grid, one per line, followed by the status row and
then the hotkey row (`interactive/StatusLine.hpp`): `formatStatusLine()` gives the summary-or-prompt
row, `formatHotkeyLine()` the key legend below it. They used to be one line (the legend appended to
the summary), but that meant the legend vanished behind the range-edit prompt, since the two shared
a single reserved row; splitting them into two fixed rows keeps the keys visible no matter what the
status row is currently showing. Which pane the keys act on is shown physically rather than in text: a
`*` in the left gutter marks the row `j`/`k` moves, and a `*` on the line below the image marks the
column `c`/`r` change. `interactive/Overlay.hpp` turns the header lines and `buildFrame()`'s reported
pane geometry into those marker positions; `Screen::pixelGeometry()` reserves the rows and gutter
column they need before the grid is fitted, so the image box and the overlay always agree.

Ranges are **typed, not scaled**: `r` prompts for `low high` (or `low,high`) on the status row, for
the active column only. On real data the numbers you want are known, and hunting for them by
repeated multiplication was slower than typing them. An entry that will not parse keeps the prompt
and the typing and says why. While the prompt is open every key belongs to it, including the
navigation letters and `Esc` — typing a number must not also move a slice.

Colour maps default to Spectral for the first volume and grayscale for the rest: the first is
usually the one being judged and the others are references it is compared against. `--colourmap`
overrides, with one name for all of them or one per file.

`s` saves whatever `draw()` last put on screen to `screenshotNNNNNN.png` in the current directory
— the exact naming convention `new_register`'s own `P` key uses (`Interface::saveScreenshot()`,
`new_register/src/Interface.cpp`): 1-based, zero-padded to six digits, continuing past any
existing file rather than filling a gap left by a deleted one. `ViewState::handleKey()` reports it
as `KeyResult::Screenshot`, a value distinct from `Changed`, since nothing in `ViewState` changes —
there is nothing new to render, only the frame already drawn to write out — so `Session::runSession()`
calls a separate `ScreenshotSink` and loops back for the next key without touching `FrameSink`.
`render/Screenshot.cpp` does the actual PNG encoding, reusing the `stb_image_write.h` copy already
vendored for the Kitty graphics protocol's PNG payload (`render/Encode.cpp`) — no new dependency.
The status row briefly shows "Screenshot saved: `<filename>`" (or a failure message) in place of the
usual summary, for the one `draw()` call `cli/Run.cpp` issues right after saving — there is no
separate notification row, since the status row is a single reserved line (`interactive/StatusLine.hpp`)
that already swaps its content wholesale for the range prompt. The hotkey row underneath is
unaffected either way, since it never depended on what the status row was showing.

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

That reprint assumes the alternate screen's own last image is already gone by the time it draws —
otherwise the two overlap. `notcurses_stop()`'s bitmap-clearing on teardown is terminal-dependent, not
guaranteed (see `mriv/HANDOFF.md`), so `Screen::destroy()` does not rely on it: it destroys the last
image plane and forces a `notcurses_render()` to flush that deletion to the terminal before
`notcurses_stop()` runs, the same way every other frame's plane-deletion rides along with the render
that draws the next frame.

The reprint also homes the cursor (`Terminal::moveCursorHome()`) before printing anything: leaving the
alternate screen restores the cursor to wherever it was before the session started, not row 0, and a
`--scale`-magnified image tall enough to nearly fill the pixel box can otherwise land with too little
room before the terminal's bottom edge and scroll into the text below it (see `mriv/HANDOFF.md`).

## Project layout

```
new_register/mriv/               # this subproject, a child of the parent's build root
├── CMakeLists.txt               # mriv_lib + mriv; notcurses lookup; cxxopts FetchContent
├── CLAUDE.md                    # how to work here
├── PLAN.md                      # this file
├── PLAN_TESTING.md              # test strategy (layers A/B/C)
├── HANDOFF.md                   # implementation notes and traps
├── cmake/
│   ├── BuildNotcurses.cmake     # -DMRIV_BUILD_NOTCURSES=ON
│   └── patch_notcurses_tinfo.cmake
├── src/
│   ├── main.cpp                 # entry point
│   ├── cli/                     # Options, Run, SliceSelection, ViewList, ColourMapArg,
│   │                            #   VolumeInfo, InteractiveDecision
│   ├── render/                  # Terminal (ncdirect), Encode, PixelProtocol.hpp,
│   │                            #   SliceGeometry, SlicePipeline, Resample, Layout,
│   │                            #   FrameBuilder, Compose, Crosshair, Screenshot
│   ├── interactive/             # Screen (full notcurses), Session, ViewState, FramePlan,
│   │                            #   RangeEditor, StatusLine, Overlay
│   └── util/                    # Base64
├── third_party/stb/             # stb_image.h, stb_image_write.h (PNG payload, screenshots)
└── tests/
    ├── CMakeLists.txt           # add_mriv_test(); mriv_test_support library
    ├── escapes.{hpp,cpp}        # escape-sequence parsing helpers
    ├── decode.{hpp,cpp}         # payload → pixels helpers
    └── test_*.cpp               # 23 suites
```

Every module is a `.hpp`/`.cpp` pair unless noted. There is no `tests/data/`: the parent's tests use the reference volumes in the repository-root `test_data/` via `TEST_DATA_DIR` (`new_register/tests/CMakeLists.txt`), and this subproject's tests use the same directory via `MRIV_TEST_DATA_DIR` (`mriv/tests/CMakeLists.txt`). Reuse those for smoke testing rather than committing new ones.

## Development environment

### Ubuntu

Only what this subproject adds; the parent's dependencies are assumed installed.

```bash
sudo apt install -y libnotcurses-dev
```

Install `libnotcurses-dev`, **not** `libnotcurses++-dev` — we use the C API only, and the C++ bindings package exists solely to provide `ncpp::`, which is banned here. `cxxopts` is fetched via CMake.

No `libnotcurses-dev`? Configure with `-DMRIV_BUILD_NOTCURSES=ON` instead — it downloads and statically builds notcurses (and its libunistring and tinfo dependencies) from source, with no system notcurses package required. See "From-source notcurses" under [Build integration](#build-integration).

For visual testing on the dev host, a pixel-capable terminal: `kitty`, `wezterm`, or Ghostty.

### macOS (Homebrew)

```bash
brew install notcurses
```

For visual testing: iTerm2, Kitty, Ghostty, or WezTerm. **Do not rely on the built-in Terminal.app** — it supports none of the pixel protocols.

### Sanity check

```bash
pkg-config --modversion notcurses    # Linux; 3.0.7 on the current dev host
```

This only reports the installed version (macOS has no `pkg-config`; check the Homebrew formula version instead); the build itself locates notcurses with `find_path`/`find_library` and does not need a `.pc` file.

## Style and formatting

See [`CLAUDE.md`](CLAUDE.md), "Coding conventions" — it follows the parent's `AGENTS.md` and adds the `mriv::term` namespace.

## Testing

The parent uses **Catch2 v3**, resolved in `new_register/CMakeLists.txt` under `ENABLE_TESTS` (system package, else fetched). Its suites are built by a small macro, `add_nr_test()` (`new_register/tests/CMakeLists.txt`), each followed by a separate `add_test()` that gives it its CTest name. With the `CATCH2` keyword the macro links `nr_test_main`, which supplies `main()`; the test source holds only `TEST_CASE`s. The migration from plain `assert` + `main()` is in progress (root `PLAN.md`); unmigrated suites omit `CATCH2`. The parent macro accepts an `ARGS` keyword but silently ignores it. Do not add doctest, GoogleTest or a second framework.

This subproject wraps that in `add_mriv_test()` (`mriv/tests/CMakeLists.txt`). It adds the `src/` and stb include dirs, links `mriv_lib` and the `mriv_test_support` helper library (`tests/escapes`, `tests/decode`), and registers the CTest entry itself, forwarding `ARGS`:

```cmake
add_mriv_test(test_foo CATCH2)                  # builds tests/test_foo.cpp, CTest name mriv_test_foo
add_mriv_test(test_bar ARGS <fixture-path>)     # arguments reach the test's argv
```

CTest names are therefore always `mriv_`-prefixed, so `ctest -R "^mriv_"` selects this subproject's 23 suites.

- **Prefer testing this subproject's logic** — CLI parsing, resampling math (including the `slicePixelAspect()` correction), terminal wrapping, axis→`viewIndex` mapping. Do not re-test `Volume::load()` or `renderSlice()`; the parent already does.
- Where parent APIs are expensive in tests, a synthetic `Volume` is cheap: `Volume::generate_test_data()` exists for exactly this, and `tests/sq1.mnc` / `sq2.mnc` are tiny real volumes.
- Integration tests exercise the full path from CLI invocation to encoded output. Capture the escape-sequence output to a buffer and assert on structure, not exact bytes.
- No test needs a real terminal. Setting `MRIV_TEST_RENDER` (the CLI integration tests do it themselves) swaps the one-shot path's ncdirect `Terminal` for a test-mode one that writes Kitty graphics escapes to a stream; `MRIV_TEST_RENDER=none` forces the no-pixel-support branch instead. What a real emulator paints is checked by hand. See `PLAN_TESTING.md`.
- **No absolute paths in test source** (`AGENTS.md`, "Hard rules") — pass data paths as arguments (or environment) from CMake using `CMAKE_CURRENT_SOURCE_DIR` / `MRIV_TEST_DATA_DIR`.

## Things Claude should NOT do

See [`CLAUDE.md`](CLAUDE.md), "Things not to do".

## Deferred work — blocked on the parent

These features appear in earlier drafts of this document. They are **not** cancelled, but they cannot be built here, because there is no parent API to call. Each needs a parent-side capability to land first.

### DICOM series handling (`--series`, `--list-series`)

**Status: blocked.** `grep -ri dicom new_register/src new_register/include` returns nothing. The parent reads MINC2 and NIfTI only — there is no DICOM reader, no series scanner, no `SeriesInstanceUID` grouping, and no directory-aware reader of any kind.

**Unblocked when:** the parent gains DICOM support behind `Volume::load()` (or a sibling directory-scanning API). At that point `mriv` adds `--series` / `--list-series` as a thin CLI layer over it. Until then, positional arguments are files only, and passing a directory should be a clean error.

Do not add DCMTK or GDCM to this subproject to work around this. That is precisely the fork this document exists to prevent.

### 4D volumes and time navigation (`-t` / `--time`)

**Status: blocked.** `Volume::dimensions` is a `glm::ivec3` (`Volume.h:21`) — the parent's volume model is strictly three-dimensional. The MINC2 path indexes only `MINC2_DIM_X/Y/Z` (`Volume::load()` in `src/Volume.cpp`) and the NIfTI path reads only `nx, ny, nz` (`src/NiftiVolume.cpp`). A 4th dimension is silently dropped by both readers.

**Unblocked when:** the parent's `Volume` grows a time/vector dimension and `renderSlice()` accepts a time index. Until then there is no `-t` flag and no `h`/`l` interactive keys.

## Milestones

The order the subproject was built in, keeping it runnable within the parent's build throughout. M1–M5 are done; M6 is partly done.

1. **M1 — Skeleton and parent integration. Done.** `mriv/CMakeLists.txt` builds an empty `mriv` binary that prints `--help` via cxxopts and links `nr_core` + notcurses. Parent-side `add_subdirectory()` lands as its own commit. One trivial `mriv_`-prefixed test passes under the parent's `ctest`.
2. **M2 — Non-interactive render path. Done.** `Terminal` wrapper around the notcurses C API, aspect-correct resampler, wired to `Volume::load()` + `renderSlice()`. `mriv file.mnc` displays the middle axial slice. Manual smoke test on Kitty against the volumes in `test_data/`.
3. **M3 — CLI surface. Done.** `--info` mode (modelled on `tests/dump_vol.cpp`), `--axis` / `--slice` selection, `-R`/`--range` / `--auto-window` (replacing the originally planned `--window` / `--level`), `--colourmap`.
4. **M4 — Multi-file rendering. Done.** Multi-file display (planned as a strip, now the [display grid](#the-display-grid)), `--max-width`, and the no-pixel-support fallback path. The `--require-pixels` flag shipped here and was later removed (`ce895c7`); the debug-only `MRIV_REQUIRE_PIXELS` environment variable replaces it.
5. **M5 — Interactive TTY mode. Done.** Keyboard navigation minus time navigation, since extended with multi-file columns, `--views`, the crosshair, the range prompt and screenshots.
6. **M6 — Partly done.** Done: the self-built static binary (`MRIV_BUILD_NOTCURSES=ON`) builds and runs on macOS (`700b3d6`). Open: the `mriv_*` suites and the system-notcurses path on macOS, standalone-build support (`MRIV_STANDALONE`), a mriv `README.md`, docs polish.

Each milestone lands with tests and leaves the parent's build runnable.
