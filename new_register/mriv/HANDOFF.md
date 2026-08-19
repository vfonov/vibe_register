# HANDOFF — implementing `mriv`

**To:** the agent implementing the terminal slice viewer.
**From:** the agent that reconciled `PLAN.md` / `CLAUDE.md` against the real parent project.
**Status of the subproject when this was written:** `new_register/mriv/` contains **three markdown files and no code**. Everything below is greenfield.

Read this file **after** `CLAUDE.md` and `PLAN.md`, not instead of them. They define *what* to build and *how to work*. This file exists for one reason: during the doc-reconciliation pass I verified every parent API against the source and found a set of traps that are invisible from the specs and will each cost you an hour. They are in [§3](#3-traps-verified-the-hard-way). Read that section before writing a line of CMake.

Everything asserted here was checked against the working tree on 2026-08-17, with the file and line numbers given. If a line number has drifted, the symbol name is still correct — re-grep, don't guess.

---

## 1. Scope of this handoff

Implement **M1 through M6** of `PLAN.md`'s milestone list, in order, stopping at each milestone with a green `ctest`.

| Milestone | Deliverable | Detail level here |
|---|---|---|
| M1 | Skeleton: `mriv` binary, `--help`, links `nr_core` + notcurses, one passing test | Full — [§4](#4-m1--skeleton-and-parent-integration) |
| M2 | Non-interactive render path: `mriv file.mnc` shows the middle axial slice | Full — [§5](#5-m2--the-non-interactive-render-path) |
| M3 | `--info`, `--axis`, `--slice`, `--window`/`--level`/`--auto-window`, `--colourmap` | Full — [§6](#6-m3--cli-surface) |
| M4 | Multi-file strip, `--max-width`, `--require-pixels`, no-pixel fallback | Outline — [§7](#7-m4m6--outline). **Done, 2026-08-18** — see the status log, including the *later* correcting entry; the first M4 entry's "green ctest" was not real. |
| M5 | Interactive TTY mode | Outline — [§7](#7-m4m6--outline). **Done, 2026-08-18** — see the status log; `Screen` is unverified on a real terminal. Extended the same day by the multi-view / multi-volume grid (last status entry), which changed what both render paths draw. |
| M6 | macOS check, standalone build, README | Outline — [§7](#7-m4m6--outline) |

**Do not start M5 before M2–M4 are end-to-end green.** `CLAUDE.md` says this and it is the single easiest way to waste a day here.

If you get through M3 and the context is running out, stop at a green milestone boundary and write a short status note at the bottom of this file. A half-finished M4 is worse than a clean stop after M3.

---

## 2. Pre-flight

Run this first. Every line must succeed before you write code. If any fails, fix that before touching `mriv/`.

```bash
cd /app/new_register/build
cmake .. && make -j$(nproc)            # must exit 0
ctest --output-on-failure              # must be 27/27
pkg-config --modversion notcurses      # must print 3.0.7
```

The parent build was reconstructed from scratch recently and pins HDF5/NetCDF/zlib to the minc-toolkit at `/opt/minc/1.9.18.9`. **Do not touch dependency detection in `new_register/CMakeLists.txt`** — see `AGENTS.md` §3 for why the ordering there is load-bearing. Your parent-side edit is *two lines* (§4.3) and goes at the very end of the file.

Sanity-check that the network is up, because M1 fetches cxxopts:

```bash
git ls-remote https://github.com/jarro2783/cxxopts.git HEAD    # verified working
```

---

## 3. Traps, verified the hard way

### 3.1 `add_nr_test()` is visible from `mriv/`, but its variables are not

`add_nr_test` is a **macro** defined at `new_register/tests/CMakeLists.txt:25`. CMake macros are registered globally, so a directory added *after* `tests/` can call it. I verified this with a throwaway two-subdirectory project: the macro resolves fine from a sibling directory.

The same experiment showed the other half: **directory-scoped variables come through empty.** `SRC_DIR`, `COMMON_INCLUDES` and `TEST_DATA_DIR` are `set()` in `tests/` scope (lines 7–9) and are `""` when the macro body runs in `mriv/tests/`. The macro's fallback branch is:

```cmake
if(T_INCLUDES)
    target_include_directories(${NAME} PRIVATE ${T_INCLUDES})
else()
    target_include_directories(${NAME} PRIVATE ${COMMON_INCLUDES})   # ← empty for you
endif()
```

**Consequences you must handle:**

1. **Always pass `INCLUDES` explicitly.** Never rely on the default. In practice `INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src` is enough, because `nr_core` carries its own include dirs as `PUBLIC` (`CMakeLists.txt:606`).
2. **Define your own `TEST_DATA_DIR`.** `set(MRIV_TEST_DATA_DIR ${PROJECT_SOURCE_DIR}/../test_data)` — that is what `tests/CMakeLists.txt:9` resolves to.
3. **Do not pass `SOURCES`.** That branch prefixes with the empty `${SRC_DIR}` and will silently produce a bad path. List extra sources by linking a library instead (§4.2 recommends exactly this).
4. **Guard the call.** `add_subdirectory(tests)` sits inside `if(ENABLE_TESTS)` (`CMakeLists.txt:832…975`). With `-DENABLE_TESTS=OFF` the macro does not exist. Wrap your test registration in `if(COMMAND add_nr_test)` and print a `message(STATUS ...)` when skipping, so a tests-off build doesn't hard-error.

### 3.2 Where `add_subdirectory(mriv)` must go

Two constraints, both pointing at the same place:

- `nr_core` is defined at `CMakeLists.txt:605`, inside `if(NOT BUILD_QC_ONLY)` which spans lines **360–741**.
- `add_nr_test` only exists after `add_subdirectory(tests)` at line **856**, inside `if(ENABLE_TESTS)` which ends at line **975** (end of file).

So the call goes **at the end of the file, after line 975**. Anywhere earlier and you lose the test macro.

### 3.3 `ncvisual_from_rgba` argument order is a trap

```c
struct ncvisual* ncvisual_from_rgba(const void* rgba, int rows, int rowstride, int cols);
```
(`/usr/include/notcurses/notcurses.h:3266`)

The middle argument is **rowstride, not cols**, and rowstride is **in bytes**. For a tightly packed RGBA buffer of `w × h`:

```cpp
ncvisual_from_rgba(pixels.data(), h, w * 4, w);
```

Getting this wrong gives a skewed or torn image rather than an error, and you will blame your resampler for an hour.

### 3.4 `colourMapByName()` matches display names, exactly

`PLAN.md` documents `--colourmap <name> (default: grayscale)`. That string will **not** resolve. `colourMapByName()` (`src/ColourMap.cpp`, ~line 428) does an exact `std::string_view` comparison against `colourMapName()`, whose values are:

```
Gray, Hot Metal, Cold Metal, Green Metal, Lime Metal, Red Metal, Purple Metal,
Spectral, Red, Green, Blue, Contour, Viridis, Jet, Magma, Inferno, Plasma, Turbo
```

Note `"Gray"` — not `"GrayScale"`, not `"grayscale"` — and the two-word names with a space.

**Do this in the CLI layer, not in the parent:** normalise the user's argument (lowercase, strip spaces/underscores/hyphens) and compare against the same normalisation of `colourMapName(i)` for `i` in `[0, colourMapCount())`. That makes `--colourmap hotmetal`, `hot-metal`, `Hot Metal` all work. On no match, print the full list of accepted names to `std::cerr` and exit non-zero. Add a test for the normalisation — it is pure logic that lives in this subproject, exactly the kind of thing `CLAUDE.md` wants tested.

### 3.5 The exact `viewIndex` → geometry mapping

From `src/SliceRenderer.cpp`, in the `renderSlice()` body:

| `viewIndex` | Plane | Slices along | `result.width` | `result.height` | `slicePixelAspect()` args |
|---|---|---|---|---|---|
| 0 | axial | Z (`dimensions.z` slices) | `dimensions.x` | `dimensions.y` | `(0, 1)` |
| 1 | sagittal | X (`dimensions.x` slices) | `dimensions.y` | `dimensions.z` | `(1, 2)` |
| 2 | coronal | Y (`dimensions.y` slices) | `dimensions.x` | `dimensions.z` | `(0, 2)` |

The aspect args are **not** derivable from `viewIndex` by a simple formula — write the 3-row table as a `constexpr` array in the CLI or render layer and index it. Getting `(1, 2)` and `(0, 2)` swapped produces a plausible-looking but wrong image on anisotropic data.

`--axis x|y|z` maps to `viewIndex` `1|2|0` respectively. `z` is the default. Do the mapping once, in the CLI layer.

### 3.6 Rows are already flipped — do not flip again

Each of the three branches in `renderSlice()` writes to `dstOff = (h - 1 - <row>) * w`. The returned buffer is already in **top-down display order**, which is what `ncvisual_from_rgba()` expects. If your image comes out upside down, the bug is in your resampler, not a missing flip.

### 3.7 `renderSlice()` returns an empty struct on an empty volume

First lines of the function:

```cpp
if (vol.data.empty())
    return result;      // width = 0, height = 0, pixels empty
```

It does **not** throw. Check `width > 0 && height > 0` before handing anything to notcurses; a zero-sized `ncvisual` is undefined behaviour territory.

### 3.8 `Volume::load()` throws

`void Volume::load(const std::string&)` (`include/Volume.h:57`) throws `std::runtime_error`. Catch at the CLI boundary, print to `std::cerr`, exit non-zero. `src/mincpik/mincpik_main.cpp:152` and `:620` are the parent's own pattern — copy it.

There is no directory-scanning reader. A directory passed as a positional argument must produce a clean error, not a crash (`PLAN.md`, deferred-work section).

### 3.9 The dev machine cannot display pixels

`TERM=xterm` here. `notcurses_check_pixel_support()` will return `NCPIXEL_NONE`. **You cannot visually confirm anything.** Plan for this from M2 — see the test strategy in §5.4. Do not let "I'll eyeball it later" become the verification plan; it will never happen in this environment.

### 3.10 Standing repo rules that apply to you

- **Never touch `legacy/`.**
- **No absolute paths in test source** (`AGENTS.md` §7.9). Pass data paths as argv from CMake using `${CMAKE_CURRENT_SOURCE_DIR}` / your `MRIV_TEST_DATA_DIR`.
- **Never install or modify system packages.** notcurses 3.0.7 is already present. If you think you need another package, ask the user.
- `std::cerr` for errors, never `printf`. C++17. Allman braces, 4 spaces. `PascalCase` types, `camelCase` methods and variables. Namespace `mriv::term`.

---

## 4. M1 — skeleton and parent integration

### 4.1 Files to create

```
new_register/mriv/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   └── cli/
│       └── Options.hpp / Options.cpp
└── tests/
    ├── CMakeLists.txt
    └── test_options.cpp
```

### 4.2 `mriv/CMakeLists.txt`

Structure it as **a static library plus a thin executable**. This is not gold-plating — it is the only way to unit-test CLI parsing and resampling without linking `main()` into every test, and it sidesteps the `SOURCES`/`SRC_DIR` trap in §3.1.3.

```cmake
# mriv — terminal slice viewer.  Builds as a subdirectory of new_register.
find_package(PkgConfig REQUIRED)
pkg_check_modules(NOTCURSES REQUIRED IMPORTED_TARGET notcurses)

include(FetchContent)
FetchContent_Declare(cxxopts
    GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
    GIT_TAG        v3.3.1)
set(CXXOPTS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CXXOPTS_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cxxopts)

add_library(mriv_lib STATIC
    src/cli/Options.cpp
    # src/render/*.cpp added in M2
)
target_include_directories(mriv_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(mriv_lib PUBLIC nr_core cxxopts::cxxopts)
target_link_libraries(mriv_lib PRIVATE PkgConfig::NOTCURSES)

add_executable(mriv src/main.cpp)
target_link_libraries(mriv PRIVATE mriv_lib)

if(ENABLE_TESTS)
    add_subdirectory(tests)
endif()
```

Notes:
- `pkg_check_modules(... IMPORTED_TARGET ...)` gives you `PkgConfig::NOTCURSES` with the right `-lnotcurses -lnotcurses-core`. `find_package(PkgConfig)` is already called at `CMakeLists.txt:78`, but call it again here so a standalone build works.
- notcurses is `PRIVATE` on purpose: `CLAUDE.md`'s header-hygiene rule says this subproject's headers must not force notcurses includes on consumers. Keep `<notcurses/notcurses.h>` out of every `.hpp`.
- `nr_core` is `PUBLIC` so tests linking `mriv_lib` get `Volume.h` and friends transitively.
- Pin the cxxopts tag. Do not track a branch.

### 4.3 The parent-side edit — its own commit

Append to the **end** of `new_register/CMakeLists.txt` (after line 975, `endif() # ENABLE_TESTS`):

```cmake
# --- mriv: terminal-based slice viewer (subproject) ---
# Placed last: it needs nr_core (line ~605) and, for its tests, the
# add_nr_test() macro defined by add_subdirectory(tests) above.
option(BUILD_TERMINAL_VIEWER "Build the terminal-based slice viewer (mriv)" ON)
if(BUILD_TERMINAL_VIEWER AND NOT BUILD_QC_ONLY)
    add_subdirectory(mriv)
endif()
```

`PLAN.md` suggests putting the `option()` next to the others at lines 24–27. Keeping it adjacent to the `add_subdirectory()` is clearer and equally correct; either is fine, but the `add_subdirectory()` itself is not movable.

This is the **only** parent-source change you are authorised to make. It ships as a standalone commit (`CLAUDE.md`, change hygiene).

### 4.4 `mriv/tests/CMakeLists.txt`

```cmake
if(NOT COMMAND add_nr_test)
    message(STATUS "mriv: add_nr_test() unavailable (ENABLE_TESTS=OFF) — skipping mriv tests")
    return()
endif()

set(MRIV_TEST_DATA_DIR ${PROJECT_SOURCE_DIR}/../test_data)

add_nr_test(test_options
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
    LINKS    mriv_lib
)
add_test(NAME mriv_options COMMAND test_options)
```

Every CTest name **must** start with `mriv_` so `ctest -R "^mriv_"` selects the suite.

### 4.5 M1 definition of done

```bash
cd /app/new_register/build && cmake .. && make -j$(nproc)
./mriv --help                          # cxxopts-generated help, exit 0
ctest -R "^mriv_" --output-on-failure  # ≥1 test, all pass
ctest --output-on-failure              # still 27/27 parent tests + yours
```

The last line matters: a broken `add_subdirectory` placement typically shows up as parent tests vanishing from the list, not as an error.

---

## 5. M2 — the non-interactive render path

Goal: `mriv /app/test_data/mni_icbm152_t1_tal_nlin_sym_09c.mnc` renders the middle axial slice.

### 5.1 The pipeline, with the parent boundary marked

```
argv ──► Options (cxxopts)                                    mriv
      ──► Volume vol; vol.load(path)                          nr_core   [throws]
      ──► VolumeRenderParams p; p.valueMin/valueMax           mriv builds, from
          from Volume::computeQuantile(0.02 / 0.98)           nr_core primitive
      ──► RenderedSlice s = renderSlice(vol, p, viewIndex,    nr_core
                                        sliceIndex)
      ──► resampleToDisplay(s, aspect, maxW, maxH)            mriv   ◄── the real work
      ──► Terminal::blit(rgba, w, h)                          mriv
```

Everything left of `resampleToDisplay` is a function call into `nr_core`. If you find yourself opening a file, indexing `vol.data` for display, or building a colour LUT, stop and re-read `CLAUDE.md`.

### 5.2 The resampler — write this test-first

This is the one piece of genuine logic in the subproject, and `PLAN.md` flags skipping it as a hard failure. Specification:

Given `RenderedSlice{w, h}` and `a = vol.slicePixelAspect(axisU, axisV)` (verified at `src/Volume.cpp`: returns `|step[axisU]| / |step[axisV]|`, and `1.0` if the denominator underflows):

```
effectiveW = w * a          // slice width in units of one step[axisV]
effectiveH = h
scale      = min(maxW / effectiveW, maxH / effectiveH)
outW       = max(1, lround(effectiveW * scale))
outH       = max(1, lround(effectiveH * scale))
```

Nearest-neighbour sampling (bilinear is a later refinement, not M2):

```
srcX = clamp((int)((dx + 0.5) * w / outW), 0, w - 1)
srcY = clamp((int)((dy + 0.5) * h / outH), 0, h - 1)
out[dy * outW + dx] = in[srcY * w + srcX]
```

`a` corrects the *voxel* aspect. Do not also ask notcurses to scale — blit with `NCSCALE_NONE` onto a plane sized from your output, or pass no plane at all and let notcurses size it exactly. `NCSCALE_SCALE` preserves the *pixel* aspect you already fixed, and stacking the two is how you end up with a subtly wrong image that still looks fine on isotropic data.

### 5.3 Concrete test values

`test_data/mni_icbm152_t1_tal_nlin_sym_09c_thick_slices.mnc` is deliberately anisotropic and is your aspect fixture. Verified with `tests/dump_vol`:

```
dims : 64 x 229 x 96      step : 3.0  1.0  2.0
```

Derived expectations — assert these directly:

| View | `viewIndex` | slice `w × h` | aspect args | `a` | `effectiveW × effectiveH` |
|---|---|---|---|---|---|
| axial | 0 | 64 × 229 | `(0,1)` | 3.0 | 192 × 229 |
| sagittal | 1 | 229 × 96 | `(1,2)` | 0.5 | 114.5 × 96 |
| coronal | 2 | 64 × 96 | `(0,2)` | 1.5 | **96 × 96** |

The coronal case is the gift: the corrected output must be **exactly square**. `assert(outW == outH)` is an exact, non-fragile assertion that fails loudly the moment the aspect correction is dropped or the axis pair is swapped. Make that your first red test.

Also assert the negative control: on an isotropic volume (`Volume::generate_test_data()` gives 256³ with `step = 1,1,1`, `src/Volume.cpp`) every `a` is `1.0` and `outW/outH == w/h`. That pins the correction as a no-op where it should be one, so nobody "simplifies" it away.

Cheap fixtures, in order of preference: `Volume::generate_test_data()` (no I/O, `include/Volume.h:67`), then `new_register/tests/sq1.mnc` (21 KB), then the `test_data/` volumes (15–33 MB — fine for one smoke test, too slow for a unit test).

### 5.4 Testing the terminal layer without a terminal

Per §3.9 you have no pixel protocol. Structure the render layer so this doesn't block you:

- **Keep `Terminal` thin.** It takes `(const uint32_t* rgba, int w, int h)` and nothing else — no `Volume`, no `RenderedSlice`. Everything above it is then testable without notcurses at all, which is where the vast majority of your tests should live.
- **`notcurses_init()` accepts a `FILE*`** (`notcurses.h:1087`). Point it at a `tmpfile()` or an `fmemopen()` buffer and you can drive the full blit path headlessly, then assert on *structure* — non-empty output, contains an escape byte `\x1b` — not exact bytes. `PLAN.md` says this explicitly; honour it, terminal output is not byte-stable across versions.
- Use `NCOPTION_SUPPRESS_BANNERS | NCOPTION_DRAIN_INPUT | NCOPTION_NO_ALTERNATE_SCREEN` (`notcurses.h:1015`, `:1030`) in tests so nothing waits on input or clobbers scrollback.
- Gate anything that needs a real pixel terminal on `MRIV_TEST_RENDER=1`, skipped by default.

### 5.5 Sizing the output box

```c
void ncplane_pixel_geom(const struct ncplane* n,
                        unsigned* pxy, unsigned* pxx,
                        unsigned* celldimy, unsigned* celldimx,
                        unsigned* maxbmapy, unsigned* maxbmapx);
```
(`notcurses.h:1419`)

Call it on the standard plane. `pxy/pxx` is the plane's size in pixels — that is your `maxH`/`maxW` before applying `--max-width`. **`maxbmapy`/`maxbmapx` are `0` when there is no limit**, so only clamp when they are non-zero; clamping to zero yields a 1×1 image and a very confusing bug report.

`ncvisual_geom()` (`notcurses.h:3393`) is the more general solver and is worth knowing about, but `ncplane_pixel_geom()` is sufficient for M2.

### 5.6 RAII, per `CLAUDE.md`

`Terminal` owns the `struct notcurses*` and calls `notcurses_stop()` in its destructor; the `ncvisual*` is owned by a scope guard calling `ncvisual_destroy()`. No raw `new`/`delete`. No exceptions across the notcurses boundary — return `bool`/`std::optional` and write the diagnostic to `std::cerr`. The parent has no `Result<T,E>`; don't go looking for one.

### 5.7 M2 definition of done

```bash
ctest -R "^mriv_" --output-on-failure    # resampler tests green, incl. the square-coronal case
./mriv /app/test_data/mni_icbm152_t1_tal_nlin_sym_09c.mnc   # exits 0; on TERM=xterm prints the
                                                            # no-pixel-support message to stderr
./mriv /nonexistent.mnc                  # clean message, non-zero exit, no stack trace
```

---

## 6. M3 — CLI surface

Implement the flags from `PLAN.md`'s CLI section. Notes that only apply once you are writing them:

- **`-W`/`-L` are sugar, not a code path.** `valueMin = level - window/2`, `valueMax = level + window/2`. Test the arithmetic; it is three lines and exactly the kind of thing that gets sign-flipped.
- **`--auto-window` is the default** and calls `Volume::computeQuantile(q)` (`include/Volume.h:59`). There is **no parent default to inherit** — `new_mincpik` only computes quantiles when the user passes `--qrange` (`src/mincpik/mincpik_cli.cpp:175`, consumed at `mincpik_main.cpp:119-120`), and otherwise leaves the range alone. So this is your choice: 2%/98% is a sane default for MRI. Whatever you pick, name the numbers in `--help` and put them in one named constant, not two literals at the call site.
- **Leave the clamp modes at their defaults.** `VolumeRenderParams` defaults both `underColourMode` and `overColourMode` to `kSliceClampCurrent` (`include/SliceRenderer.h:30-31`). `new_mincpik` overrides `underColourMode` to `kSliceClampTransparent` because it composites onto a background; `mriv` blits a single opaque image, so transparency buys you nothing and costs you a surprising black-vs-transparent difference against `new_mincpik` output.
- **`-s/--slice` accepts `n`, `p%`, or `mid`.** `mid` = `dim/2` along the slicing axis, per §3.5's table. Clamp out-of-range indices rather than erroring — `renderSlice()` clamps internally anyway, but clamp in the CLI too so `--info`-style feedback can report what was actually rendered.
- **`--info`** is modelled on `new_register/tests/dump_vol.cpp` — read it, the format is already sensible. Print `dimensions`, `step`, `start`, `dirCos`, `min_value`/`max_value`, and the world coordinates of the corners. Exit before touching notcurses. This is the one mode that works perfectly on this dev host, so it is also your best end-to-end smoke test.
- **`--colourmap`** — see §3.4. Normalise, and list valid names on error.

Test the argument→state mapping (a pure function from `argv` to an options struct) exhaustively; that is cheap and is where the bugs are. Do not test that `renderSlice()` honours `valueMin` — the parent already does.

---

## 7. M4–M6 — outline

- **M4 — strip rendering.** One row per input file, `--max-width` caps the pixel width, `--require-pixels` exits non-zero when `notcurses_check_pixel_support()` returns `NCPIXEL_NONE` (`notcurses.h:1670`). Without that flag, fall back to `NCBLIT_2x2` Unicode blocks *or* exit with a clear message naming Kitty/Ghostty/WezTerm/iTerm2/Konsole — `PLAN.md` forbids silently substituting ASCII art.
- **M5 — interactive mode.** Only when stdout is a TTY *and* exactly one file was given. Keys `j/k` slice, `x/y/z` axis, `+/-` window, `q` quit. **No `h`/`l`** — there is no time axis (`PLAN.md`, deferred work). Do not start this before M4 is green.
- **M6 — polish.** `README.md` (`CLAUDE.md` requires a feature line per commit once it exists), `MRIV_STANDALONE` build, macOS verification. Standalone mode still needs a pre-built `nr_core`; it skips the ImGui/Vulkan cost, not the parent dependency.

---

## 8. Things that will look like bugs but are not

- **cxxopts here, hand-rolled argv in the parent.** Deliberate, documented in both `PLAN.md` and `CLAUDE.md`. Do not "align with the parent."
- **No test framework.** Plain `assert` in a `main()` is the house style; 27 parent tests work this way. Do not add doctest/Catch2/GoogleTest.
- **`libnotcurses++-dev` may be installed** (`/usr/include/ncpp/` exists here). It is banned — C API only.
- **No DICOM anywhere in the parent.** Not an oversight, not yours to fix. Same for 4D volumes: `Volume::dimensions` is a `glm::ivec3` (`include/Volume.h:21`) and both readers drop any fourth dimension. Do not build `--series`, `--list-series`, or `--time`.
- **`renderSlice()` already returns finished RGBA.** There is no window/level step to write, no colour mapping, no 8-bit conversion. If you are writing any of those, you have taken a wrong turn.

---

## 9. Commit plan

One concern per commit, green tests at every point in history (`CLAUDE.md`, change hygiene):

1. `build: add BUILD_TERMINAL_VIEWER option and mriv subdirectory` — parent-side only, the two lines from §4.3.
2. `mriv: skeleton CMake, cxxopts options, --help` — M1.
3. `mriv: aspect-correct nearest-neighbour resampler` — M2, test-first, the square-coronal assertion.
4. `mriv: notcurses Terminal wrapper and blit path` — M2.
5. …one per flag group for M3.

### Where you are starting from

You begin on branch **`build-hdf5-pin-and-mriv-spec`**, whose last three commits are the groundwork for this handoff:

```
build: pin HDF5/NetCDF/zlib to the detected minc-toolkit
docs: reconcile AGENTS.md, PLAN.md, problem.md and research.md with the tree
mriv: add subproject spec, working rules and implementation handoff
```

Nothing in `mriv/` is committed beyond the three markdown files. `main` does not yet have any of this — check with the user before merging or pushing.

### Never `git add -A` in this repository

The working tree carries several things that must **never** enter a commit, and a blanket add will take all of them:

- `legacy/bicgl` and `legacy/register` show as **modified submodule pointers**. They were already dirty before any of this work and `legacy/` is read-only (`AGENTS.md` §7.2). Leave them.
- `legacy/display/`, `legacy/libminc/`, `legacy/minc-tools/` — untracked working copies under `legacy/`. Same rule.
- `test_data/*.nii.gz`, `test_data/nsstx2_subject04_1_t1.mnc` — 13–33 MB untracked binaries. Not yours, and too big to commit casually.
- `.opencode/package-lock.json` — unrelated tooling.

Stage explicit paths, always.

---

## 10. Status log

Append one line per milestone as you complete it, so the next agent knows where the boundary is.

- 2026-08-17 — handoff written; `mriv/` is docs-only, no code yet.
- 2026-08-17 — parent groundwork committed on `build-hdf5-pin-and-mriv-spec` (build fix, doc reconciliation, this spec). Parent build green: 27/27 ctest. M1 not started.
- 2026-08-17 — M1 complete: `mriv` skeleton (CMake, `mriv_lib`/`mriv` split, cxxopts wiring under `mriv::term`, `--help`, positional files), one `mriv_options` test. Parent `add_subdirectory(mriv)` landed as its own commit. `ctest` green 28/28 (27 parent + 1 mriv). M2 not started.
- 2026-08-17 — M2 complete: aspect-correct nearest-neighbour resampler (`render/Resample.*`), the `--axis`/`viewIndex`/`slicePixelAspect()`-axis-pair table (`render/SliceGeometry.*`, pinned against the real thick-slices fixture), and a notcurses `Terminal` RAII wrapper (`render/Terminal.*`) with an `initCli()` using `NCOPTION_CLI_MODE`. `main.cpp` wires `Volume::load()` -> `computeQuantile(0.02/0.98)` -> `renderSlice()` (hardcoded axial/mid-slice; `--axis`/`--slice` land in M3) -> `resampleToDisplay()` -> `Terminal::blit()`. `mriv <volume.mnc>` on this host (no pixel-capable terminal) exits 0 with a clear stderr message; a nonexistent path exits 1 with a clean message, no stack trace. Also fixed a build trap: the parent forces `CMAKE_FIND_LIBRARY_SUFFIXES` to prefer `.a` globally, but notcurses's static archive is missing several transitive deps (qrcodegen, gpm, libdeflate) from its `.pc` `Libs.private`; `mriv/CMakeLists.txt` now locally overrides the suffix order before `pkg_check_modules(notcurses)` so it resolves the `.so`. `ctest` green 31/31 (27 parent + 4 mriv: options, slice_geometry, resample, terminal). M3 not started.
- 2026-08-17 — M3 complete: full CLI surface wired test-first, one small module per concern under `src/cli/`: `SliceSelection.*` (parse/resolve `n`/`p%`/`mid`), `ColourMapArg.*` (normalised `--colourmap` name matching per HANDOFF sec 3.4, plus `listColourMapNames()` for error messages), `WindowLevel.*` (the `-W`/`-L` -> valueMin/valueMax arithmetic), `VolumeInfo.*` (`--info` formatting, modelled on `tests/dump_vol.cpp` but a pure string-returning function tested against `Volume::generate_test_data()`, no I/O). `Options.{hpp,cpp}` grew to the full flag set (`-a/--axis`, `-s/--slice`, `-W/--window`, `-L/--level`, `--auto-window`, `-c/--colourmap`, `--invert`, `--require-pixels`, `--max-width`, `-i/--info`, `--version`), validating each flag at parse time (invalid axis/slice/colourmap values, or `-W` without `-L`, or `--auto-window` combined with `-W`/`-L`, are all rejected with a `std::cerr` message and `ok=false`) rather than deferring validation to render time. `main.cpp` now resolves `--axis` -> `viewIndex` -> slicing-axis dimension -> `--slice` -> concrete slice index, builds `VolumeRenderParams` from either `--window`/`--level` or the auto-window quantiles, applies `--colourmap`/`--invert`, and honours `--max-width` (clamped against the terminal's own pixel box) and `--require-pixels` (exit 1 instead of 0 when there's no pixel protocol). `--info` prints via `std::cout` and returns before touching notcurses, as specified. One naming trap hit and fixed: a test file named `test_volume_info.cpp` collided with the parent's own CTest target of the same name (`new_register/tests/test_volume_info.cpp`) — `add_executable` target names are global across the whole CMake project, not just the mriv subdirectory, so it had to be renamed `test_mriv_volume_info.cpp`; worth remembering for any future mriv test name that might shadow a parent test. `ctest` green 35/35 (27 parent + 8 mriv: options, slice_selection, colourmap_arg, window_level, volume_info, slice_geometry, resample, terminal). M4 not started.
- 2026-08-17 — Bug reported by the user against a real Kitty terminal (`TERM=xterm-kitty`): `--info` worked, but plain `mriv <volume>` produced *no visible output at all* (not even an error). Root cause: `Terminal::blit()`'s `ncvisual_options` was zero-initialized, so `y`/`x` defaulted to 0 — with no `ncplane` given to `ncvisual_blit()`, that places the *new* plane relative to the standard plane's origin, not the current cursor position. Combined with `NCOPTION_PRESERVE_CURSOR` (which only restores the physical cursor to wherever it was at `notcurses_init()` time), the image got drawn at row 0 while the terminal's actual cursor stayed put — so the next shell prompt landed right back where it started, visually erasing any evidence the image was ever drawn. Fixed in `render/Terminal.cpp` by querying the std plane's live cursor row via `ncplane_cursor_yx()` and using it as `vopts.y`, then advancing the std plane's cursor past the blitted plane's row count (`ncplane_dim_yx()` on the plane `ncvisual_blit()` returns) so later output doesn't land on top of the image. Added `Terminal::cursorRow()` and a regression test (`testBlitPlacesImageAtCursorAndAdvancesPastIt`) asserting the cursor advances past its starting row after a successful blit — like the existing structural blit test, it can only exercise the real assertion on an actual pixel-capable terminal, which this sandbox still doesn't have (HANDOFF.md sec 3.9), so it's a guard for the next environment that can run it, not a repro here. **This is exactly the kind of bug sec 3.9 warned about** — nothing in the M1–M3 test suite could have caught it without a real pixel-capable terminal; if you touch `Terminal::blit()` again, get a human to eyeball it in a real Kitty/WezTerm/iTerm2 session, don't trust green `ctest` alone. `ctest` still green 35/35 after the fix. M4 not started.
- 2026-08-17 — Testing plan from `PLAN_TESTING.md` implemented across three layers instead of starting M4. **Layer A (structure):** `test_structure.cpp` plus `tests/escapes.{hpp,cpp}` parses escape-sequence output and asserts on Kitty graphics framing and cursor-move events; `Terminal(std::ostream&, PixelProtocol)` constructor bypasses notcurses for byte-level tests. **Layer B (pixels):** `test_pixels.cpp` plus `tests/decode.{hpp,cpp}` builds synthetic volumes, runs them through `renderSlice()` -> `resampleToDisplay()` -> Kitty PNG encode -> base64 decode -> `stb_image` PNG decode, and asserts on decoded image dimensions and pixel properties (uniform axial mid-slice, horizontal gradient in sagittal, checkerboard preserved, auto-window non-degenerate, `--max-width` cap respected). A latent bug in `computeResampleSize()` was caught here: it scaled images *up* to the terminal box, so a 16x16 slice became 4096x4096 under the default cap; capped `scale` at `1.0` so native-sized slices stay native unless `--max-width` forces a smaller cap. One test name updated from `XGradient` to `YGradient` because the sagittal plane displays Y vs Z, not X. **Layer C (CLI):** `test_cli.cpp` invokes the new `mriv::term::run(argc, argv, in, out, err)` seam with injected streams, checking `--help`, `--version`, unknown flags, missing files, `--info` output, full render pipeline produces exactly one Kitty image, `--max-width` shrinks the image, and `--axis` changes the encoded bytes. To make this testable, `run()` was extracted to `src/cli/Run.{hpp,cpp}`, `main.cpp` became a one-liner, and `MRIV_TEST_RENDER=1` forces the Kitty test-mode `Terminal`. Fixed `test_slice_geometry` CMake wiring: `add_nr_test()` ignores the `ARGS` keyword, so the fixture path is now passed via `set_property(TEST ... PROPERTY ENVIRONMENT ...)` and read from `MRIV_THICK_SLICES_FIXTURE` in the test. `VolumeInfo.cpp` output changed the `dims` label to `dimensions:` to match the `test_info` assertion. Final state: `ctest` green 38/38 (27 parent + 11 mriv). Production render path on this sandbox still reports no pixel support and exits 0 as before.

### 2026-08-17 — real-Kitty "no visible output" bug, round 2: root-cause investigation and fix plan

The M4-not-started entry above ("Bug reported by the user...") turned out to be an *incomplete* fix. The user re-tested against a real `kitten`/Kitty session (not this no-pixel sandbox) and reported that `mriv <volume.mnc>` still shows no image at all, even though `kitten icat test.png` works fine in the same terminal.

**Added `MRIV_DEBUG=1` verbose logging** (`src/cli/Run.cpp`, `src/render/Terminal.cpp`) gated behind an env-var check, logging every step of `run()` and `Terminal` (parse result, volume dims, axis/slice resolution, value range, `notcurses_check_pixel_support()`'s raw return value, `ncplane_pixel_geom()`'s raw fields, cursor position, blit/render return codes). This is left in place — it is zero-cost when `MRIV_DEBUG` is unset and has already paid for itself twice.

With `MRIV_DEBUG=1` in the user's real Kitty session, every step reports success:

```
notcurses_check_pixel_support returned 5    (NCPIXEL_KITTY_SELFREF -- real pixel support)
std plane cursor at row=42 col=0            (terminal is 43 rows tall: 1720px / 40px cell height)
ncvisual_blit at y=0 x=0 -> succeeded
notcurses_render returned ok
terminal.blit returned true
```

...and yet nothing is visible. Two placement fixes were already attempted in the prior entry and in this session (`vopts.y = cursorY`, then `vopts.y = 0`); both "succeed" per the log and both are invisible in the real terminal. That ruled out y/x placement as the (sole) cause and pointed at something happening *after* the successful render, i.e. at teardown.

**Root cause, confirmed against the notcurses headers themselves (`/usr/include/notcurses/notcurses.h`):**

```c
// to do this, pass NCOPTION_NO_CLEAR_BITMAPS. Note that they might still
// get cleared even if this is set, and they might not get cleared even if
// this is not set. It's a tough world out there.
#define NCOPTION_NO_CLEAR_BITMAPS    0x0002ull
```

`mriv`'s `Terminal` is a stack-local object in `run()`; its destructor calls `notcurses_stop(nc_)` immediately after the successful blit, right before the process exits. `notcurses_stop()` restores terminal state and, per this doc comment, may delete just-drawn bitmaps as part of that restoration -- **even with `NCOPTION_NO_CLEAR_BITMAPS` set**, which is one of the four flags baked into `NCOPTION_CLI_MODE` that `Terminal::initCli()` already uses. This is explicitly called out as unreliable/terminal-dependent in the header, not something we're doing wrong at the call-site level. It explains every observation: no crash or error anywhere in the log (the draw genuinely succeeds), and both placement attempts failed identically because placement was never the active bug -- the teardown wipes the bitmap either way. It also explains why `kitten icat` is unaffected: it's a one-shot process that writes Kitty escape codes directly to stdout and exits, without running any "restore the terminal to a pristine state" teardown routine that might delete what it just drew.

Secondary finding, now believed *not* to be the active bug but recorded for completeness: `notcurses_stdplane()`'s doc comment states "its origin is always at the uppermost, leftmost cell of the terminal" -- i.e. std-plane row 0 is always *whatever the terminal currently shows as its top row*, not a historical/scrollback-relative coordinate. So the `vopts.y = 0` fix already applied in this session should be placement-correct; it just doesn't matter because of the teardown issue above.

**Planned fix (not yet implemented -- next agent/session should implement this):**

Switch the real-terminal (non-test-mode) rendering backend in `Terminal` from the full `notcurses_*` / `ncplane` / `ncvisual_blit` API to notcurses's **`ncdirect_*` API** (`<notcurses/direct.h>`), which is explicitly documented as the tool for exactly this use case:

> "Direct mode supports a limited subset of Notcurses routines which directly affect 'fp' ... This can be used to add color and styling to text in the standard output paradigm." (`ncdirect_init()`)
> "The image may be arbitrarily many rows -- the output will scroll -- but will only occupy the column of the cursor, and those to the right." (`ncdirect_render_image()` / `ncdirectf_render()` + `ncdirect_raster_frame()`)

That is a precise description of `kitten icat`'s behavior, and `ncdirect_stop()`'s doc comment carries no equivalent of the bitmap-clearing caveat -- direct mode doesn't manage a "screen" to restore, so (per the plan's hypothesis) there is nothing for it to clear.

Concrete per-method mapping planned for `render/Terminal.{hpp,cpp}`:

| Current (`notcurses`) | Planned replacement (`ncdirect`) |
|---|---|
| `notcurses_init()` / `notcurses_stop()` | `ncdirect_init()` / `ncdirect_stop()` |
| `notcurses_check_pixel_support()` | `ncdirect_check_pixel_support()` (same `NCPIXEL_*` enum) |
| `ncplane_pixel_geom(stdplane, ...)` | `ncdirectf_geom(nc, frame, vopts, &geom)` -> reads `geom.maxpixely/maxpixelx`. **Open problem:** this call requires a non-null `frame` argument (`__attribute__((nonnull (1, 2)))`), so `pixelGeometry()` needs a throwaway probe `ncvisual` (e.g. a 1x1 RGBA buffer) purely to query geometry before the real image size is known -- a chicken-and-egg step that doesn't exist in the current `ncplane_pixel_geom()` path. |
| `ncplane_cursor_yx(stdplane, ...)` | `ncdirect_cursor_yx(nc, &y, &x)` |
| `ncvisual_blit()` + manual `y/x` placement + `notcurses_render()` + manual cursor-advance-past-image bookkeeping | `ncdirectf_render(nc, frame, vopts)` -> `ncdirect_raster_frame(nc, ncdv, NCALIGN_LEFT)`. Placement and scroll-advance are handled internally by `ncdirect_raster_frame()` -- no more manual `ncplane_cursor_move_yx()` bookkeeping. `ncvisual_from_rgba()` is unchanged (an `ncdirectf` is a `typedef`'d `ncvisual`), so the resampling/encoding code upstream of `Terminal::blit()` does not change. The caller must still free the frame itself after rastering (`ncdirectf_render()`'s doc: "A loaded frame may be rendered in different ways before it is destroyed" -- ownership stays with the caller, unlike the `ncdirectv*` that `ncdirect_raster_frame()` frees on all paths). |

**Open questions the next session must resolve before implementing, not assume:**

1. **`test_terminal.cpp`'s test strategy may not survive this change.** It currently redirects notcurses at a `tmpfile()` (not a real tty) specifically so the M1-era structural tests can run without a pixel-capable terminal (sec 5.4 / sec 3.9). `direct.h` states outright: *"'fp' must be a tty."* If `ncdirect_init()` enforces this at runtime, that whole test strategy for the real (non-test-mode) path breaks and needs a pseudo-terminal (`openpty()`) instead of `tmpfile()`, or loses coverage. This should be checked experimentally as step one of the implementation, not assumed either way.
2. **`testBlitPlacesImageAtCursorAndAdvancesPastIt`** (added in the prior round-1 fix, asserting `cursorRow()` advances after a successful blit) is written against notcurses/`ncplane` semantics and will need to be rewritten around `ncdirect_cursor_yx()` -- and possibly dropped/changed, since `ncdirect_raster_frame()` manages cursor advancement itself rather than leaving it to the caller.
3. **Scope decision:** whether `Terminal` should fully switch to `ncdirect` (single backend, matches the documented use case, recommended) versus keeping `notcurses` as a fallback/dual backend for some reason (adds real complexity, no identified benefit).
4. **Recommended first step, regardless of the above:** a cheap, throwaway confirmation of the root-cause hypothesis before committing to the `ncdirect` rewrite -- e.g. temporarily keep the `Terminal` (and its notcurses context) alive past the blit (block on a read, or delay the destructor) and check whether the image appears and then visibly vanishes the instant `notcurses_stop()` runs. This is a few lines, fully reversible, and would turn "the header says this might happen" into a confirmed repro before spending the effort on the `ncdirect` rewrite.

No production code changes were made in this session beyond the `MRIV_DEBUG=1` logging (which is a net positive, low-risk addition and should stay). The `vopts.y = cursorY` -> `vopts.y = 0` placement change from earlier in this session is still in the tree; it is very likely placement-neutral-but-harmless per the "origin is always at the uppermost, leftmost cell" finding above, and should be revisited/removed if the `ncdirect` rewrite supersedes it. `ctest` green 38/38 (unchanged by the debug logging). M4 not started.

### 2026-08-17 — real-Kitty "no visible output" bug, round 3: `ncdirect` rewrite implemented

Resolved the two open questions from round 2 experimentally (both against this sandbox's notcurses 3.0.7, no real terminal needed) before writing any production code, per round 2's item 4:

- **Open question 1 (tty enforcement):** false alarm. `ncdirect_init(nullptr, tmpfile(), ...)` returns a valid non-null context despite `direct.h`'s "'fp' must be a tty" doc comment -- verified with a throwaway standalone program. The existing `tmpfile()`-based test strategy in `test_terminal.cpp` did not need to change to a pseudo-terminal.
- **`ncdirect_cursor_yx()` on a non-responding `tmpfile()`:** also checked, since its doc comment warns it "requires writing to the terminal, and then reading from it" and results "might be deleterious" if it doesn't reply. It does not hang; it returns -1 promptly. Safe to keep calling from tests and debug logging.
- **`ncdirectf_geom()` requires a non-null loaded frame** (confirmed via its `nonnull(1,2)` attribute and by calling it with one) -- round 2's open problem was real. `pixelGeometry()` now builds a throwaway 1x1 RGBA probe `ncvisual` purely to read `ncdirect_dim_x()/ncdirect_dim_y()` (terminal size in cells) times `ncvgeom::cdimy/cdimx` (cell size in pixels, from the probe call) back out, clamped to `ncvgeom::maxpixely/maxpixelx` when the terminal reports a limit -- the same "clamp only when non-zero" rule as the old `ncplane_pixel_geom()` path (sec 5.5).
- **`ncdirectf_render()` + `ncdirect_raster_frame()`** verified end-to-end against a `tmpfile()` (probe RGBA -> render -> raster -> non-empty output containing an ESC byte, no crash), confirming round 2's mapping table works as designed.

**Scope decision (open question 3):** full switch to `ncdirect`, no dual backend -- `Terminal` no longer touches full notcurses (`ncplane`/`ncvisual_blit`/`notcurses_render`) at all; `<notcurses/direct.h>` replaces `<notcurses/notcurses.h>` as the primary API (`notcurses.h` is still included for `ncvisual_from_rgba`/`ncvisual_destroy`/`ncvgeom`, which are shared types). This was going to be the recommendation regardless, but there's now a sharper reason to prefer it over a dual backend: the user's actual goal for `mriv` is an **interactive TUI** (M5), and full notcurses's whole-screen model — the very thing whose teardown caused this bug — is what M5 will need (alternate screen, live redraw loop, `notcurses_render()` per frame). Building a dual-backend `Terminal` now would mean speculatively half-building M5's plumbing before M4 is even done, which `CLAUDE.md` rules out directly ("Do not implement caching, prefetching, or interactivity before the non-interactive path is end-to-end green"). Practically, M5 is a different problem shape (live loop vs. one-shot print-and-exit) and will almost certainly want a separate class when it lands, not a mode flag on this one. This is recorded in a doc comment at the top of `Terminal.hpp` so it doesn't get relitigated by a future session that only sees "why doesn't this use full notcurses for M5."

**Implementation**, `src/render/Terminal.{hpp,cpp}`:

| Method | Old (full notcurses) | New (`ncdirect`) |
|---|---|---|
| `init()`/`initCli()` | `notcurses_init()`/`notcurses_stop()`, `NCOPTION_CLI_MODE` composition | `ncdirect_init()`/`ncdirect_stop()`; `initCli()` now just passes `NCDIRECT_OPTION_DRAIN_INPUT` -- direct mode has no alternate-screen/clear-bitmaps/preserve-cursor knobs to compose, it never manages a "screen" in the first place |
| `hasPixelSupport()` | `notcurses_check_pixel_support()` | `ncdirect_check_pixel_support()` (same `NCPIXEL_*` enum, shared between both APIs) |
| `pixelGeometry()` | `ncplane_pixel_geom(stdplane, ...)` | probe-frame + `ncdirectf_geom()` + `ncdirect_dim_x/y()`, as above |
| `cursorRow()` | `ncplane_cursor_yx(stdplane, ...)` | `ncdirect_cursor_yx()`; returns 0 on query failure instead of leaving the out-params untouched |
| `blit()` | `ncvisual_blit()` with manual `vopts.y/x` + manual `ncplane_cursor_move_yx()` bookkeeping + `notcurses_render()` | `ncdirectf_render()` -> `ncdirect_raster_frame(..., NCALIGN_LEFT)`, which frees the rendered frame and handles placement/cursor-advance internally. All the manual y/x and cursor-move code from both round-1 and round-2 is gone -- there is no longer anything for that class of bug to happen to. |

`test_terminal.cpp` updated: flags switched from `NCOPTION_*` to `NCDIRECT_OPTION_*` (`kTestFlags` now mirrors `initCli()` exactly: `NCDIRECT_OPTION_DRAIN_INPUT`). `testBlitPlacesImageAtCursorAndAdvancesPastIt` renamed to `testBlitSucceedsAndCursorAdvancesWhenQueryable` and its doc comment rewritten to explain that the manual-placement bug it was pinning down is now structurally impossible rather than just fixed; the `after > before` assertion is kept (gated on both `blitOk` and a successful cursor query, since `tmpfile()` still can't answer the cursor-position query) for whenever it does run against a real terminal.

**Unrelated pre-existing test failure fixed along the way:** `ctest` was not actually 38/38 green at the start of this session despite the prior entry's claim -- the very last commit before this session (`127fae7`, "cap resampling at native size") changed `computeResampleSize()`'s behavior (capped `scale` at `1.0` so slices aren't upscaled past their native size) but didn't update three of `test_resample.cpp`'s assertions to match, so `mriv_test_resample` was failing on a clean checkout before any of this session's changes. Fixed the three stale expected values (sagittal, coronal-square, isotropic-ratio cases) to match the intentional native-size-cap behavior; the invariants each test is actually checking (squareness, ratio preservation) still hold, only the magnitudes needed updating. Worth flagging: green-`ctest` claims in this log should be spot-checked, not trusted blindly, at the start of a session.

**Verified in this sandbox** (still no pixel-capable terminal, so this is not a substitute for a real-Kitty check): `ctest` green 38/38. `MRIV_DEBUG=1 ./mriv/mriv <volume>` shows `ncdirect_init` succeeding, `ncdirect_check_pixel_support` correctly reporting 0 (`NCPIXEL_NONE`), the existing "no pixel graphics protocol" message and exit 0 unchanged, and `mriv/mriv /nonexistent.mnc` still failing cleanly with exit 1 and no stack trace -- i.e. no regression in the paths this sandbox *can* exercise.

**This still needs a real Kitty/WezTerm/iTerm2 session to confirm the actual fix** -- per HANDOFF sec 3.9, nothing in this sandbox can validate that the image now stays visible after the process exits, which is the entire point of the round-1/round-2/round-3 investigation. That is the next step, not further code changes. M4 not started.

### 2026-08-18 — round 3 confirmed fixed on real Kitty; `-W`/`-L` replaced with `-R`/`--range`, `--scale` added

The user confirmed the `ncdirect` rewrite from the previous entry fixes the real-Kitty invisible-image bug: the image now stays visible after `mriv` exits. Round 1/2/3 of that investigation is closed.

Before continuing toward M4/M5, the user asked for two CLI changes (test-first, per `CLAUDE.md`):

- **`-W`/`-L` (window/level) replaced outright by `-R`/`--range <low,hi>`.** This supersedes §6's original spec note ("`-W`/`-L` are sugar, not a code path") and the M3 CLI table above -- both describe the flag set as it stood before this change, not the current one. `-R`/`--range` maps `low`/`high` directly onto `VolumeRenderParams::valueMin`/`valueMax`, no arithmetic in between (simpler than the window/level conversion it replaced, so `cli/WindowLevel.{hpp,cpp}` and its test were deleted rather than kept as a second code path). Parsed as a `cxxopts::value<std::vector<double>>` (comma-delimited, e.g. `-R 10,200`) and validated at parse time: exactly two values, and `low < hi` strictly (this is new -- the old `-W`/`-L` pair had no equivalent ordering check, since window/level naturally avoids degenerate ranges but a raw low/hi pair does not). Still an error to combine with `--auto-window`.
- **`--scale <n>` added: integer pixel magnification.** Implemented in `render/Resample.hpp`/`.cpp` as a new `scale` parameter (default 1) on `resampleToDisplay()`, not in the CLI/`Run.cpp` layer, so it's covered by `test_resample.cpp`'s existing unit-test style rather than only through the CLI-integration tests. Semantics: fit into `(maxW/scale, maxH/scale)` as before (so the native-size cap from `127fae7` still applies to the *pre-magnification* size), then replicate each resulting pixel into a `scale x scale` block via the existing `resamplePixelsNearest()` -- reusing the same nearest-neighbour primitive that already does upsampling elsewhere, not a new code path. Verified in test mode (`MRIV_TEST_RENDER=1`, synthetic 4096x4096 terminal box) that `--scale 3` exactly triples both the blitted width and height for `sq1.mnc`, and manually via `MRIV_DEBUG=1` that a 100x100 native slice becomes 200x200 with `--scale 2`.

One test-design trap hit while adding `test_cli.cpp`'s `--range` coverage: `sq1.mnc`'s voxel values are exactly `{0, 1}` (`tests/dump_vol` output), so a naive `--range 0,1` test produced *identical* output to the default `--auto-window` range (both clamp the same two values to full black/white) and the "changes the image" assertion passed for the wrong reason -- it wasn't exercising `--range` at all. Fixed by using `--range -1,2`, which straddles both data values without touching either, so `1` maps to a partial (not fully saturated) shade and the images genuinely differ. Worth remembering for any future `sq1.mnc`-based intensity-mapping test: this fixture's binary value range makes range/window tests trivially pass unless the chosen range lands strictly inside `[0, 1]`.

`ctest` green 37/37 (one fewer than the last entry's 38, since `mriv_test_window_level` was deleted, not replaced 1:1 -- there is no separate arithmetic module for `-R` to unit-test, the low/hi -> valueMin/valueMax mapping is a direct assignment covered by `test_options.cpp` and `test_cli.cpp`). `PLAN.md`'s CLI surface section and `PLAN_TESTING.md`'s suggested-test list updated to match; §6 and the M3 table above are left as historical spec/status and now read as superseded by this entry, not edited in place. M4 still not started.

- 2026-08-18 — **M4 complete: multi-file strip rendering.** `--max-width` and `--require-pixels` were already implemented in a prior session; the only missing piece was `Run.cpp`'s hard rejection of more than one input file (`"mriv: multiple files are not yet supported"`). Removed that and replaced the single-file dispatch in `run()` with a loop over `parsed.options.files`, test-first per `CLAUDE.md` (`test_cli.cpp`: `testMultipleFilesProduceStrip`, `testMultipleFilesInfoPrintsEach`, `testOneMissingFileAmongManyFails` -- confirmed red against the old rejection message, then green).
  - **`--info` with multiple files** now loads and prints `formatVolumeInfo()` for each file in argument order, before the terminal is touched at all (unchanged early-return shape, just wrapped in a loop).
  - **Render mode** inits the `Terminal` once, then loops: load volume, `renderAndBlitVolume()`, blit. `Terminal::blit()` already places each image at the *current* cursor position and advances past it internally (`ncdirect_raster_frame()`, see the round-1 bug entry above) -- calling it N times in a row stacks N images vertically with zero extra cursor bookkeeping in this subproject. This is why the strip "just works" as a loop rather than needing an explicit newline/cursor-move between images.
  - **Per-file height budget:** `renderAndBlitVolume()` gained a `stripCount` parameter; `maxH` is now `terminalBoxHeight / stripCount` (still respecting `--max-width` on the width side, which is unaffected by strip count) so an N-file strip divides the terminal's vertical space evenly instead of every row trying to claim the full screen height. `--max-width` continues to apply per-row on top of that.
  - **First-failure-aborts semantics:** if any file in the list fails to load (`Volume::load()` throws), `run()` prints `"mriv: failed to load '<path>': ..."` and returns 1 immediately -- it does not render the files that loaded successfully before the bad one. Simplest correct behaviour; nothing in `PLAN.md` asks for partial-strip output on error.
  - Deliberately did **not** implement `PLAN_TESTING.md` item 4's suggested `CursorMove` escape events between strip images -- those events model full-notcurses-style explicit `ESC[row;colH` placement, which this subproject's `ncdirect`-based `Terminal` doesn't use (see the `Terminal.hpp` doc comment on why `ncdirect_raster_frame()` handles placement/advancement internally, no manual `y`/`x`). The test instead asserts strip structure the way this `Terminal` actually produces it: exactly one `KittyGraphics` event per input file, in order. `PLAN_TESTING.md` is a suggestions list from before the `ncdirect` rewrite, not a binding spec -- left as-is rather than edited, since it's still useful signal for *what* to test, just not literally *how*.
  - Verified manually in test mode (`MRIV_TEST_RENDER=1`): `mriv sq1.mnc sq1.mnc sq1.mnc` produces exactly 3 `ESC _ G` introducers in the byte stream; `mriv --info sq1.mnc sq1.mnc` prints `dimensions:` twice.
  - `ctest` green 37/37 (no test count change -- three tests added to `test_cli.cpp`, no tests removed or split into new binaries).
  - **M5 (interactive TTY mode) is next**, now that M2-M4 are all green per `CLAUDE.md`'s ordering rule. Not started this session.

- 2026-08-18 (later) — **Correction to the M4 entry above, and the defects that entry's "green ctest" hid.** A review pass of the M4 work found that its central verification claim was false, plus four real defects in the strip path. All are fixed; the M4 entry above should be read together with this one.

  **The `ctest` result was hollow — this is the important part.** `mriv/tests/CMakeLists.txt` registered the CLI suite as `add_mriv_test(test_cli ARGS .../sq1.mnc)`, but `add_nr_test()` (`new_register/tests/CMakeLists.txt:25`) *declares* the `ARGS` keyword in its `cmake_parse_arguments` and then **never uses it** — it only builds the executable and never calls `add_test`. `add_mriv_test`'s own `add_test(NAME mriv_${NAME} COMMAND ${NAME})` then registered the test **with no arguments**, which the generated `build/mriv/tests/CTestTestfile.cmake` confirmed verbatim. So under `ctest` the binary ran with `argc==1`, `fixturePath==nullptr`, and `test_cli.cpp`'s `if (fixturePath)` guard **silently skipped 9 of its 14 tests** — every test that touches rendering, i.e. all three M4 strip tests *and* the `--range`/`--scale`/`--axis`/`--max-width`/render-pipeline tests added in the two sessions before it. `mriv_test_cli` reported "Passed" while asserting only argument-parsing trivia. The M4 tests had only ever passed because they were run by hand with the path appended. The tell was in the timing all along: the test took 0.02 s before the fix and 0.80 s after, because it had never once loaded a volume.

  Fixed by making `add_mriv_test` parse `ARGS` itself and forward it — `add_test(NAME mriv_${NAME} COMMAND ${NAME} ${M_ARGS})` — which repairs the keyword for every future mriv test rather than working around it a second time (`test_slice_geometry` already worked around it with a CTest `ENVIRONMENT` property, and its warning comment, now updated, is what `test_cli` ignored). `test_cli.cpp`'s `main()` now does `assert(argc > 2 && ...)` like `test_slice_geometry.cpp:70` instead of skipping silently, so a wiring regression can never hide again. **This is the second time a green `ctest` has given false assurance here** (the round-1 `Terminal::blit()` placement bug was the first). The durable lesson: when a test's cost doesn't match what it claims to do, that is evidence, not noise.

  **Defects found and fixed in the strip path**, all in code no test covered:
  - **The no-pixel message printed once per file, after wasted work.** `hasPixelSupport()` was checked inside `renderAndBlitVolume()`, i.e. per file, and *after* `vol.load()` + `renderSlice()` — so a 3-file strip on a non-pixel terminal printed the same 3-line message three times, each after loading and rendering a volume purely to discard it. Hoisted into `run()`, once, before any load. `--require-pixels` now also fails fast.
  - **The `stripCount` height division (`maxH = box.height / stripCount`) was untested and unobservable.** Test-mode `pixelGeometry()` is a fixed 4096x4096, `computeResampleSize()` caps scale at 1.0 so images never upscale, and `sq1.mnc` renders 100x100 — `4096/N` never binds, so the division had *zero* observable effect in any test or manual check. **Removed entirely** rather than tested: per user decision, every row now gets the full height budget exactly as a single-file render does. Terminals scroll, so N images need not share one screen, and a volume should not change size according to how many siblings were on the command line. `Resample.hpp`'s "never exceeds (maxW, maxH)" contract is accurate again.
  - **A mid-list load failure aborted the run** after already blitting the earlier rows, leaving a truncated strip plus exit 1. Now a failed file is reported and skipped; the run exits non-zero only if *nothing* rendered. The existing `mriv: failed to load '<path>'` wording is kept ("skipping" reads oddly for a single-file invocation), so a lone bad file still exits 1 exactly as before.
  - **`pixelGeometry()` was probed once per file**; in real mode each probe allocates a throwaway 1x1 `ncvisual` and runs `ncdirectf_geom()`. Queried once in `run()` and passed down — terminal geometry cannot change during a one-shot run.

  **New: per-row captions.** Each strip row is now captioned with its path (as typed on the command line — no basename extraction), via a new `Terminal::printLine()`. Routing captions through `Terminal` rather than `std::cout` keeps text and image bytes on one output path: in test mode both land in the injected stream in the right order, and in real mode both go through the same `FILE*` instead of interleaving `std::cout` with ncdirect's stdio writes. A single-file run is left uncaptioned, so the "cat for medical images" case stays pure image bytes and `testRenderPipelineProducesKittyImage`'s `events.size() == 1` assertion stays meaningful.

  **Test seams and strengthening.** `MRIV_TEST_RENDER=none` now selects `PixelProtocol::None` (any other non-empty value stays Kitty, preserving every existing test), which is what finally makes the no-pixel and `--require-pixels` branches reachable from Layer C — their unreachability is exactly why the duplicate-message defect survived. The strip tests were also passing **the same fixture repeatedly**, so every payload was identical and they could only count images, never catch a reordering or a dropped file; they now use `sq1.mnc` *and* `sq2.mnc` (100x100 vs 50x50, so the payloads genuinely differ) and assert strictly alternating caption/image events with the captions in argument order. `testMissingFileFails` was silently environment-dependent — with the pixel check now ahead of the load, a no-pixel host never reached the load at all — so it forces `MRIV_TEST_RENDER=1` and tests what it means to test.

  `ctest` green 37/37, and for the first time that number actually covers the CLI suite: `mriv_test_cli` grew from 5 effective tests to 18. Verified manually that a 3-file strip on a no-pixel terminal now emits exactly one message and loads zero volumes, that a bad path no longer aborts the strip (exit 0, one image) while an all-bad list still exits 1, and that captions appear in argument order with a single file left uncaptioned. **Still needs a human eyeball on a real Kitty/WezTerm/iTerm2 session** (HANDOFF sec 3.9): nothing here can confirm how the captions actually sit against the images once `ncdirect_raster_frame()` advances the cursor — test mode has no cursor emulation, so caption *placement* is the one part of this that remains unverified. M5 (interactive TTY mode) is unblocked once that visual check passes.

- 2026-08-18 (later still) — **M5 complete: interactive TTY mode.** Built in five commits, each green, structured so that as much of M5 as possible is testable on this host and the untestable remainder is too thin to hold a bug.

  **The shape of the work was driven by sec 3.9.** Nothing in this sandbox can run a real terminal, and this subproject has now had two green-`ctest`-but-broken incidents. So M5 was split into pure modules that are fully unit-tested plus one glue class that is not, rather than one class that does everything and can only be checked by eye:
  - `interactive/ViewState.{hpp,cpp}` — the key handling. No notcurses, no `Volume`, no IO. `test_view_state.cpp`.
  - `interactive/Session.{hpp,cpp}` — `runSession()`, the loop, parameterised over a `KeySource` and a `FrameSink` so it can be driven from a scripted key string. `test_session.cpp`.
  - `interactive/StatusLine.{hpp,cpp}` and `cli/InteractiveDecision.{hpp,cpp}` — the status row and the entry decision. `test_interactive.cpp`.
  - `render/SlicePipeline.{hpp,cpp}` — `renderSliceForDisplay()`, extracted from `Run.cpp` so the one-shot path and the interactive loop share one copy of the `slicePixelAspect()` correction. `test_slice_pipeline.cpp`.
  - `interactive/Screen.{hpp,cpp}` — the only untestable file. Full notcurses glue, deliberately decision-free. Its header says so; keep it that way, because logic added there is logic that will not be tested.

  **`Screen` uses full notcurses, not `ncdirect`** — the prediction in `Terminal.hpp`'s doc comment turned out right, for a sharper reason than "M5 will want a redraw loop". Redrawing in place requires *releasing the previous frame's bitmap before drawing the next*, which is exactly the sprixel lifecycle notcurses owns; doing it over `ncdirect`'s cursor movement would mean hand-rolling that. And the round-2/3 teardown bug does not apply here: it was about `notcurses_stop()` wiping a bitmap that had to survive process exit, whereas interactive mode runs on the alternate screen, where restoring the terminal on exit is the wanted behaviour. `render/Terminal` (ncdirect) is untouched and still owns the one-shot path.

  **Design decisions worth not relitigating:**
  - *Position is a 3D voxel cursor*, not one slice index rescaled between views. The three axes are geometrically independent — "60% along Z" has no meaning on X — so each axis keeps its own position and `x` then `z` returns exactly where you were. Rescaling by fraction was tried on paper first and cannot even round-trip `mid`, since `resolveSliceIndex()`'s `mid` is `dimSize / 2`, which is not the true centre for even dimensions.
  - *`+`/`-` scale the window multiplicatively about its centre.* The midtone does not drift, and the range can never collapse or invert however long `-` is held. A degenerate range (a constant volume, where both auto-window quantiles coincide) reports `Ignored` rather than producing NaN.
  - *`handleKey()` returns `Ignored` when nothing changed* — unbound key, current axis reselected, slice clamped at either end — and `runSession()` skips the repaint. A frame is a full slice render plus a bitmap upload; without this, holding `j` at the top of a stack is a redraw storm.
  - *An explicit `--interactive` that cannot be honoured is refused with a reason and exit 1*, not silently downgraded to a one-shot render, which would look to the user like the keys were broken. `--no-interactive` is the escape hatch for someone at a TTY who wants one-shot output anyway.
  - *The no-pixel diagnostic is deferred until after `Screen` is destroyed.* Anything printed while the alternate screen is up is wiped by the restore.

  **A weak assertion caught by mutation, worth copying as a habit.** `testExplicitInteractiveWithoutATtyIsRefused` originally asserted only that stderr contained `"terminal"`. Deleting the refusal branch from `decideInteractive()` did not fail it: without the refusal the run *entered* interactive mode, failed the pixel-support check, and printed "this terminal has no pixel graphics protocol" — which also contains `"terminal"`. The test passed while asserting the opposite of what it meant. Both refusal tests now assert their own exact wording. **Mutating the code and confirming the test dies is cheap; on this subproject it has now paid off three times.**

  **What is verified and what is not.** `ctest` green 41/41 (27 parent + 14 mriv; `test_view_state`, `test_session`, `test_interactive` and `test_slice_pipeline` added, `test_cli` grew six dispatch tests). Verified under a real pseudo-terminal (`pty.fork()`, `TERM=xterm-kitty`) that argument parsing, the interactive decision, the volume load and the `ViewState` setup all run in order and that a bad path errors *before* the screen is taken over. **`notcurses_init()` blocks on a bare pty** — it needs a terminal that answers its capability handshake, and faking enough of one was not worth the effort — so everything from `Screen::init()` onward is unverified here: the status row's placement against the image, whether the previous frame's bitmap is really released on redraw, and whether `readKey()` sees one event per press rather than two. **That is the visual check to ask a human for, on real Kitty/Ghostty/WezTerm/iTerm2, before M6.** It now supersedes the caption-placement check the previous entry left pending, since both are answered by the same session.

  **One behaviour change to be aware of:** plain `mriv volume.mnc` on a terminal is now *interactive*, per `PLAN.md`'s CLI surface. The one-shot path a human previously confirmed on real Kitty is still what runs when stdout is piped, when several files are given, or with `--no-interactive` — but it is no longer what the bare command does. If interactive mode misbehaves on a real terminal, flipping the auto-detection default to off is a one-line change in `decideInteractive()`, and `--interactive` still reaches it.

  M6 (README, `MRIV_STANDALONE` build, macOS verification) is next.

- 2026-08-18 (later still) — **Multi-view / multi-volume display.** Not a milestone: a feature request that landed on top of M5 and reshaped what both render paths draw. Ten commits, each green.

  **What changed, from the user's side.** All three planes are shown at once, stacked vertically, narrowed by a new `--views <list>`; multiple input files become columns rather than a vertical strip, with slice navigation synchronised across them; the `+`/`-` window keys are gone, replaced by an `r` prompt that takes typed `low high` values per column, plus `c`/`C` to cycle that column's colour map; the first volume defaults to Spectral and the rest to grayscale; and quitting now leaves the last frame on the terminal instead of a restored blank screen.

  **The grid is one bitmap, and that is load-bearing.** `render/Layout.hpp` divides the pixel box, `render/FrameBuilder.hpp` renders each `(view, volume)` cell through the existing `renderSliceForDisplay()`, `render/Compose.hpp` composites them into a single RGBA buffer. Alternatives — one notcurses plane per pane, or N `ncdirect` blits — were rejected because they push placement logic into the classes that cannot be tested here (sec 3.9). With one bitmap, `Screen` and `Terminal` are unchanged, the entire layout is a pure function with its own suite, and "leave the last frame on screen" becomes re-blitting a buffer that already exists.

  `new_mincpik`'s `blitSlice()` (`src/mincpik/mosaic.h`) does nearly what `composeGrid()` does, but it is compiled into that executable rather than into `nr_core`, so it is not linkable from here. The local version is fifteen lines; do not try to pull mincpik sources into this subproject's build to avoid them.

  **The box is a budget, not the output size.** First attempt sized the composed frame to the terminal's pixel box. In test mode that box is a fixed 4096x4096 and `computeResampleSize()` caps the fit at 1.0, so every test blitted a 64 MB canvas containing a 100x100 image. `buildFrame()` now fits each slice into its share of the box and then sizes the frame to what those fits produced. `--scale` multiplies the gutter along with the panes, or the frame's proportions would depend on the zoom level — that one showed up as `testScaleFlagMagnifiesImage` failing by exactly the gap.

  **Cross-volume sync scales by last index, not by count.** `mapSliceIndex(i, from, to)` maps the shared cursor from the first volume's axis onto another's. Scaling by `to/from` looks natural but does not map the last slice to the last slice when the target is deeper, so a cursor parked at the top of one stack would not be at the top of the other. Scaling by `(to-1)/(from-1)` aligns both ends exactly and costs half a slice at the middle — `mapSliceIndex(25, 50, 100) == 51`, which is worth the comment it has in the test. Equal counts short-circuit to an exact identity, because the common case is the same subject in two modalities and "the same slice" there has to mean the same index.

  **Decisions worth not relitigating:**
  - *`--axis` no longer changes the one-shot picture.* Every plane is drawn now; `--axis` selects which one `--slice` positions and `j`/`k` moves. `testAxisChangesImage` was replaced by `testViewsChangeImage` plus a test that `--axis` still steers `--slice`, rather than deleted.
  - *An axis whose view is not displayed cannot be made active*, and an `--axis` outside `--views` falls back to the first displayed view. Otherwise `j`/`k` would move a slice nobody can see and the keys would look broken.
  - *Interactive mode no longer cares how many files there are.* A terminal on stdout is the whole requirement; several volumes are columns of one navigable grid, which is more use interactively than piped.
  - *Every keystroke in the range prompt repaints the whole frame.* It changes the status row, and giving `Session` a second, status-only path was not worth it — the cost is one frame per character, the same as holding `j`.
  - *`Screen` stopped translating `Esc` to `'q'`.* What `Esc` means depends on whether a prompt is open, which is state that class deliberately does not have. It now returns `0x1b` and `ViewState` decides. `NCKEY_BACKSPACE`/`NCKEY_ENTER` are translated to `'\b'`/`'\r'` before the `id > 0x7f` filter drops them — without that the prompt could be typed into but never corrected or submitted.
  - *The retained exit frame goes out through `render/Terminal` (ncdirect)*, not by trying to keep the notcurses frame alive. `notcurses_stop()` clears bitmaps even with `NCOPTION_NO_CLEAR_BITMAPS` (sec 3, round 2/3); the one-shot path is the code already proven not to lose them.

  **Two of my own test expectations were wrong before the code was.** `mapSliceIndex(25, 50, 100)` I wrote as 50 (it is 51, see above), and `testEachPaneIsAspectCorrected` asserted a 1:3 pane would be 3x taller than wide when the aspect correction shrinks the narrow axis and rounds — 7x20, not 20x60, the same numbers `test_slice_pipeline` already pins. Both were fixed in the test with a comment explaining the direction, not worked around in the code. `composeGrid()` passed on its first run, so its centring was mutation-checked before being trusted; `parseRangeText()`'s trailing-junk and ordering guards likewise.

  **Verified:** `ctest` green 46/46 (27 parent + 19 mriv; `test_view_list`, `test_layout`, `test_compose`, `test_frame_builder`, `test_range_editor` added, `test_view_state` and `test_interactive` largely rewritten). Headless end-to-end through `MRIV_TEST_RENDER=1`: a three-view single volume of the anisotropic fixture composes to 192x429, exactly the per-pane aspect-corrected sizes stacked with two 4px gutters; `--views z` gives one row; two files widen the frame and produce one image with one caption naming both; `--max-width` binds and divides between columns; `--scale 2` doubles both dimensions including the gutter.

  **Unverified, and it is the same list as M5 plus the new panes:** everything from `Screen::init()` onward. Whether the grid's proportions read correctly on a real terminal, whether the status row sits where it should above a taller image, whether the range prompt is usable, and — new — whether the frame re-blitted after `notcurses_stop()` actually survives on Kitty/Ghostty/WezTerm/iTerm2. **That is still the visual check to ask a human for before M6.**

### 2026-08-19 — active-pane markers, per-volume header lines, and arrow-key navigation

Continuation of the multi-view/multi-volume grid work committed earlier (`cb4683d`–`413d574`). The grid
is one bitmap, so nothing inside it can say which row or column the keys act on; this session added the
terminal text drawn around it to answer that, plus a second way to move the selection.

**`buildFrame()` now optionally reports where the panes landed (`FrameTracks*` out-parameter).** The
layout starts from per-cell budgets and shrinks to what `renderSliceForDisplay()`'s fit actually
produced, so only `buildFrame()` knows the final column/row offsets and sizes — `computeGrid()`'s
budgets are not them. Passing `nullptr` (the default) costs nothing extra; existing callers are
unaffected. `test_frame_builder.cpp` gained two cases pinning the reported geometry and confirming a
degenerate request leaves the tracks empty rather than stale.

**New `interactive/Overlay.{hpp,cpp}` turns tracks into terminal text.** `planOverlay()` is a pure
function: given the header lines (volume names + status row), the frame's `FrameTracks`, which row/column
is active, the frame's pixel height, and the terminal's cell size, it returns a `FrameOverlay` — a list of
`(row, col, text)` cells plus where the image itself starts. The row marker is centred on the active
view's vertical midpoint (not its top edge, which would read as belonging to the row above); the column
marker sits on the first whole terminal row below the image, centred on the active column. Both are
dropped, not guessed, when the cell size is unknown or the requested index has no track — a wrong marker
would confidently point at the wrong volume, which is worse than none. Mutation-tested (removed the
centring, and the ceiling-division for the footer row) to confirm `test_overlay.cpp` actually kills both.

**`ViewState` gained `activeViewRow()`, and the arrow keys.** `--views` can list the rows in any order
(`--views y,z` is legal), so the row a marker belongs at is not the same number as `viewIndex()` — it's
the position in `views()`. Up/down step `activeViewRow()` through the displayed views and wrap; left/right
do what Tab and the digits already did for the column, also wrapping. Both pairs are carried through
`Session`/`ViewState` as the same control-code convention readline uses for the equivalent motions
(`kKeyUp = 0x10`, `kKeyDown = 0x0e`, `kKeyLeft = 0x02`, `kKeyRight = 0x06`) rather than inventing a key
variant type for four bindings; `Screen::readKey()` translates notcurses' `NCKEY_UP/DOWN/LEFT/RIGHT`
(reported above the Unicode range, like Backspace already was) into these before the `id > 0x7f` filter
would otherwise drop them. Both pairs are swallowed whole by the range prompt while it's open, same as
every other key — `testArrowsDoNotEscapeTheRangePrompt` pins that.

**`Screen`'s frame-drawing contract changed shape.** `drawFrame()` used to take one status string, printed
on a hardcoded row 0, with the image always at row 1. It now takes a `FrameOverlay` and draws every text
cell at its planned position before blitting the image at `overlay.imageRow`/`imageColumn`.
`pixelGeometry()` takes a `headerRows` count and reserves that many rows on top plus `Overlay`'s marker
row below and marker columns to the left, so the box handed to `buildFrame()` and the space `Overlay`
assumes always agree — there is exactly one place (`Run.cpp`) that decides `headerRows`, from
`formatVolumeNameLines(paths).size() + 1`, and both calls use it.

**`StatusLine` lost the inline volume-name-plus-star list.** `formatSummaryLine`/`formatStatusLine` used
to print every column's basename with a trailing `*` on the active one, on the single status row. That
information is now the header lines above the image and the physical column marker below it, so the
status row went back to just plane/slice/range/colour-map/legend. New `formatVolumeNameLines()` returns
one plain basename per line — no star; the marker is what says which one is active, not the text.
`test_interactive.cpp` updated to match: the removed assertions moved to two new
`formatVolumeNameLines()`-specific tests rather than being deleted outright.

**`Run.cpp::runInteractive()`** builds the header once per session (`nameLines` is fixed; only the trailing
status line changes per frame), gets a `FrameTracks` back from `buildFrame()`, and feeds both plus the
active row/column into `planOverlay()` before calling `screen.drawFrame()`. The "leave the last frame on
exit" path now re-prints every header line (not just one summary line) through `Terminal::printLine()`
before re-blitting — `Terminal` already supports being called multiple times, one call per line, so no
new capability was needed there.

`ctest` green 47/47 (`test_overlay` added at 7 cases; `test_frame_builder` and `test_view_state` each
gained cases; `test_interactive` net-neutral). **Still needs the same real-terminal check the M5 entry
above asked for, now covering more surface**: whether the header lines and both markers land where this
session predicts, whether the row marker's vertical centring reads right against a real image, and
whether the re-printed multi-line header survives `notcurses_stop()` the same way the single summary line
was shown to. Nothing about the marker geometry can be confirmed without eyes on a real Kitty/Ghostty/
WezTerm/iTerm2 session — `Screen` remains the one file in this area no test here can reach.

### 2026-08-19 (later) — mlterm/sixel image-not-updating bug: one fix tried and reverted, still open

Follow-up to the previous entry's active-pane-markers work. The user tested the resulting build on
two real terminals:

- **Real mlterm (sixel):** slice navigation updates the status text but never the image. Confirmed the
  bug is on the pixel/blit side, not in `ViewState`/`Session`/key handling, since the status row (driven
  by the same `handleKey()` → `runSession()` → `drawFrame()` call) does update.
- **Real Ghostty (Kitty graphics protocol):** unaffected at the time of the first report.

**First attempt (reverted): force `notcurses_refresh()` after every frame.** Hypothesis was that
`notcurses_render()`'s damage-tracked optimization was eliding the sprixel re-emission — plausible since
`ncstats` exposes exactly this (`sprixelemissions` vs `sprixelelisions` counters) and Kitty's protocol has
an explicit image id/delete model that sidesteps the ambiguity a raster-only protocol like sixel doesn't
have. `notcurses_refresh()` is documented to force a full non-optimized repaint, so it was added
unconditionally after `notcurses_render()` in `Screen::drawFrame()`, plus `MRIV_DEBUG` logging of the
`ncstats` counters to confirm or rule out the theory.

**This broke Ghostty.** Retested there and the image was not shown *at all* during the interactive
session — only the post-quit one-shot re-blit (`render/Terminal`, `ncdirect`) showed anything.
`notcurses_refresh()`'s own doc comment says it "clears the screen" before repainting, and clearing the
screen is exactly the operation that deletes a terminal's placed Kitty graphics protocol image along with
the text — so the fix likely traded "stale but visible" on sixel for "never visible" on Kitty. Reverted
in full (`3ac21b6`); the `MRIV_DEBUG` counter logging was kept since it's diagnostic-only and still useful
for the next attempt. **Retested on Ghostty after the revert: confirmed working again** — the base
interactive path (full notcurses, `ncplane`/`ncvisual_blit`/`notcurses_render`, no forced refresh) is
solid on Kitty-protocol terminals as-is.

**Lesson for the next attempt:** this is the second time in this file a fix aimed at one pixel protocol
regressed another (the first was the round-1/2/3 one-shot-path rewrite from `notcurses` to `ncdirect`,
which was protocol-neutral by construction and didn't have this problem). `notcurses.h`'s own doc comment
on `NCPIXEL_*` says "informative only; don't special-case based off any of this information" — read
together with this incident, that should be taken as "any fix must be verified not to regress the other
protocol family", not as "never write protocol-specific code": a real per-protocol difference (Kitty's
image-id model vs sixel's raster-only one) may need a per-protocol code path, and `notcurses_check_pixel_support()`'s
return value is right there for exactly that if it comes to it. **Do not land another fix here without
testing on both a sixel terminal (mlterm) and a Kitty-protocol one (Ghostty or real Kitty) first** — this
sandbox has neither (HANDOFF sec 3.9), so that verification has to happen in the same round-trip with the
user, not be assumed.

**Next step, not yet taken:** get `MRIV_DEBUG=1` output from an mlterm session (stderr redirected to a
file, since stdout carries the terminal protocol bytes) showing the `sprixelemissions`/`sprixelelisions`/
`renders`/`raster_bytes` counters across a few keypresses, to confirm or rule out the elision theory with
data before writing another candidate fix. `ctest` green 47/47, unaffected by either the addition or the
revert (`Screen` has no unit coverage — this whole bug and its fix/revert cycle happened outside anything
this sandbox's test suite can see).

### 2026-08-19 (later still) — mlterm retested working, with the revert in place

The user retested plain slice navigation on mlterm against the reverted build (no `notcurses_refresh()`,
i.e. exactly the code `Screen::drawFrame()` had before the previous entry's fix-and-revert cycle) and
reports it now works. No code changed between the original "scrolling doesn't update the image" report
and this one except the add-then-revert of `notcurses_refresh()`, which nets to no change from the
version that was originally broken — so the most likely explanation is that the original staleness was
transient session/terminal state (a stuck sprixel from earlier testing, a corrupted mlterm buffer, or
similar) rather than a reproducible bug in this code path. This is also why guessing at a fix for it
before confirming a mechanism was the wrong move, per the previous entry's lesson.

Not treating this as confirmed-fixed in the sense of "the bug is understood" — only as "not currently
reproducing, on both a sixel terminal (mlterm) and a Kitty-protocol one (Ghostty)". If it recurs, get the
`MRIV_DEBUG=1` sprixel-counter log from the previous entry before writing another candidate fix, rather
than reasoning from the symptom alone again. `ctest` green 47/47, unchanged.

### 2026-08-19 (yet later) — resize made the image disappear until the next slice change

The user found a third real-terminal bug, distinct from the mlterm one: resizing the terminal window
made the slice view disappear, reappearing only after scrolling through slices.

**Two independent bugs, both in the resize path, neither related to the mlterm investigation above.**

1. **`Screen::readKey()` swallowed `NCKEY_RESIZE` and repainted the *stale* frame at the new geometry
   itself**, via a `notcurses_refresh()` call local to the resize branch, then looped for another key
   without telling the caller anything happened. `notcurses_refresh()` repaints "the most recently
   rendered frame" — the *old*, wrong-sized image — so a resize produced a redraw of content that no
   longer matched the terminal's new cell/pixel geometry, which is what the user saw as the image simply
   vanishing (this is a different call site from the `drawFrame()` one added and reverted earlier today;
   this one predates that investigation and was never touched by it).

2. **Even if that redraw had been correct, `Run.cpp::runInteractive()` computed the display box once,
   before the session loop started**, and every frame — including ones drawn after a resize — reused
   those captured `maxW`/`maxH` values. So a `Changed` result reaching `draw()` after a resize would
   still have rebuilt the frame at the *pre-resize* size.

This is why the user's workaround (scroll a slice) appeared to fix it: `moveSlice()` triggers the normal
`Changed` → `draw()` path, but with the box captured stale, the actual repair only came from whatever
coincidental relayout happened to look plausible at the wrong size, or the user's follow-up resize hint —
either way, both defects needed fixing together for the fix to be real, not just for the symptom to move.

**Fix, test-first:**

- **`interactive/ViewState.hpp`** gained `constexpr char kKeyResize = '\x0c';` (Ctrl-L, the traditional
  Unix "redraw the screen" key — chosen deliberately: a user who mashes it when a repaint looks stuck
  gets the standard recovery for free, not a mriv-specific binding to memorize). `handleKey()` now checks
  for it *before* the `editing_` dispatch — a resize is not text and must not be typed into an open range
  prompt, close it, or touch what's in it — and returns `KeyResult::Changed` unconditionally without
  mutating any state. Two new `test_view_state.cpp` cases: `testResizeForcesARepaintWithoutChangingAnything`
  (slice/axis/active-volume all identical before and after) and
  `testResizeDuringThePromptRepaintsWithoutTouchingTheText` (prompt stays open, typed text untouched).
- **`interactive/Screen.cpp`**: the `NCKEY_RESIZE` branch no longer calls `notcurses_refresh()` or
  swallows the event — it returns `kKeyResize`, the same translate-and-forward pattern already used for
  Backspace/Enter/the arrows. notcurses has already updated its own geometry tracking by the time this
  event is returned (that's the whole point of the event existing), so the caller's next
  `pixelGeometry()` call reads the new size correctly with no extra step here — `Screen` stays exactly as
  logic-free as its header requires.
- **`cli/Run.cpp::runInteractive()`**: moved the `screen.pixelGeometry(headerRows)` call (and the
  `maxW`/`maxH` derived from it) from before the session loop to the top of the `draw` lambda, so it is
  queried fresh on every frame instead of once. `headerRows` itself is still computed once outside the
  lambda — it depends only on the volume count, which cannot change mid-session, unlike the terminal's
  pixel geometry.

No `Screen`-level or `Run.cpp`-level test exists for this (both are the untestable glue per HANDOFF sec
3.9); the fix is verified at the `ViewState` layer, where the actual decision now lives, plus a full
`ctest` pass. `ctest` green 47/47 (`test_view_state` gained the two cases above). **Still needs the user
to confirm on a real terminal** that resizing the window now keeps the image visible and correctly sized
without requiring a slice change afterward — nothing in this sandbox can drive an `NCKEY_RESIZE` event to
check it directly.

### 2026-08-19 (later again) — the resize fix above was incomplete: `pixelGeometry()` itself read stale plane dimensions

The user retested the fix above and the bug was unchanged: resize still makes the image disappear until
the next slice change. The `ViewState`/`Run.cpp` half of that fix was correct and stayed as-is; the claim
quoted above — "notcurses has already updated its own geometry tracking by the time this event is
returned... so the caller's next `pixelGeometry()` call reads the new size correctly" — was wrong. It was
never verified against notcurses' actual behavior, because `Screen` cannot be exercised in this sandbox
(no TTY, HANDOFF sec 3.9); it was a guess, and this is why guesses in this file get flagged rather than
stated as fact.

Checked this time against notcurses' own documentation (`notcurses.com` and
`/usr/include/notcurses/notcurses.h` on this host) before writing any code:

- `notcurses_refresh(3)`'s man page, on this exact situation: "primarily useful ... if an `NCKEY_RESIZE`
  event has been read and you're not yet ready to render" — reading the event does **not** by itself
  make anything current; a render or refresh afterward is still required.
- The standard plane (what `ncplane_dim_yx()`/`ncplane_pixel_geom()` read, i.e. what
  `Screen::pixelGeometry()` is built on) is resized to match the terminal only "at render time" — during
  the *next* `notcurses_render()` or `notcurses_refresh()` call, never earlier.

So the actual bug: `Run.cpp`'s `draw` lambda does call `screen.pixelGeometry(headerRows)` fresh every
frame as intended, but on the frame drawn for the resize event itself, no render has happened yet since
the resize — so that "fresh" query still reads the *pre-resize* box. The frame gets built and blitted at
the old size, and `drawFrame()`'s own `notcurses_render()` call is what finally syncs the plane to the
new size — one call too late to help that frame. The picture only looks right again once another key
produces `KeyResult::Changed` (e.g. a slice move), because by then the *previous* draw's render call
already synced the plane. Exactly "reappears only after I scroll through slices."

`notcurses_refresh()` is not the fix, even though the name suggests it — already tried and reverted this
session (commit `3ac21b6`) for the mlterm/Ghostty saga above, because its man page says it clears the
screen first, deleting placed Kitty-protocol images. Its own man page resolves this: for the "read an
`NCKEY_RESIZE`, not yet ready to render" case, it explicitly recommends calling `notcurses_render()`
instead, "to avoid unnecessary redrawing." `notcurses_render()` also runs the pending resize callback,
without the screen-clearing side effect — and it's already proven safe on both Kitty and mlterm this
session, since `drawFrame()` calls it every frame.

**Fix:** `Screen::pixelGeometry()` now calls `notcurses_render(nc_)` itself, unconditionally, before
reading `ncplane_dim_yx()`/`ncplane_pixel_geom()` — so the geometry it returns is always current whether
or not a resize is pending, rather than trusting the caller's timing. This costs one redundant render per
frame in the non-resize case, which is cheap: notcurses elides unchanged sprixel data rather than
resending it (the `sprixelemissions`/`sprixelelisions` stats logged in `drawFrame()` are what that
elision shows up as). The disproven comment on `readKey()`'s `NCKEY_RESIZE` branch was corrected to say
why no extra step belongs there instead of claiming geometry is already fresh.

Still glue-level and untestable here (HANDOFF sec 3.9) — `ctest` green 47/47 is a regression check, not
new coverage for this specific defect. **Still needs the user to confirm on a real terminal** that
resizing now keeps the image visible immediately, without a follow-up slice change.

**Confirmed working** by the user on a real terminal: resizing now keeps the image visible immediately,
no follow-up slice change needed. Committed as `c201e0c`.

### 2026-08-19 (later again still) — quitting left the last picture overlapping the text

Bug reported by the user: pressing `q` to end an interactive session leaves the last frame's picture on
screen, overlapping the text that prints after it.

Root cause is the same mechanism as the resize bug above — "a plane's destruction isn't sent to the
terminal until the next render" — applied to teardown instead of geometry queries. `runInteractive()`
(`Run.cpp`) deliberately re-prints the last frame through the one-shot `ncdirect` path after the
interactive session ends (`PLAN.md`'s "Quitting does leave the last frame on the terminal" note), on the
assumption that the alternate screen's own copy of that image is already gone from the terminal by the
time it draws. It isn't, reliably: `Screen::destroy()` (`~Screen()`) destroyed `imagePlane_` and called
`notcurses_stop()`, with no render in between —

```cpp
void Screen::destroy()
{
    if (imagePlane_)
    {
        ncplane_destroy(imagePlane_);
        imagePlane_ = nullptr;
    }
    if (nc_)
    {
        notcurses_stop(nc_);
        nc_ = nullptr;
    }
}
```

— unlike every other frame's plane-destruction in this class: `drawFrame()` destroys the *previous*
frame's plane, then later in the same call renders the new one, and that render is what actually flushes
the previous plane's deletion to the terminal (a placed pixel-protocol image's removal is not sent until
the deleting plane's destruction is rendered — the identical mechanism `pixelGeometry()`'s fix above
relies on, just for a different read). `destroy()` is the one place in `Screen` that destroys a plane
with no following render, because the session is ending — there is no next frame to carry the deletion.
It stays purely in notcurses' internal bookkeeping. `notcurses_stop()`'s own bitmap-clearing on teardown
is documented as terminal-dependent, not guaranteed (see the round 2/3 one-shot investigation above:
"may delete just-drawn bitmaps... even with `NCOPTION_NO_CLEAR_BITMAPS` set... explicitly called out as
unreliable/terminal-dependent"), and `Screen::init()` doesn't even set that flag. On a terminal where
teardown doesn't independently clear the bitmap, the old image survives the switch back to the primary
screen and sits underneath/over whatever `runInteractive()`'s ncdirect reprint prints next.

**Fix:** `Screen::destroy()` now forces the same render-to-flush step `drawFrame()` already relies on,
before `notcurses_stop()` runs — destroy `imagePlane_`, then call `notcurses_render(nc_)` to flush that
deletion to the terminal, only then call `notcurses_stop()`. `Screen.hpp`'s class doc comment, which
previously stated as fact that "restoring the terminal on exit is the wanted behaviour rather than the
bug" (implying nothing further was needed), was corrected to note that `destroy()` forces the flush
itself rather than trusting `notcurses_stop()` to do it.

Still glue-level and untestable here (HANDOFF sec 3.9) — `ctest` green 47/47 (20/20 `mriv_*`) is a
regression check, not new coverage for this defect. **Needs the user to confirm on a real terminal**
that pressing `q` now leaves exactly one picture on screen with no leftover image or overlapping text.

### 2026-08-19 (yet again) — `--scale` 2/3 still overlaps text at the bottom

Follow-up bug report on top of the fix above: at `--scale 1` the exit-time overlap is gone, but at
`--scale 2` or `--scale 3` the retained picture still covers text at the bottom of the terminal.

Ruled out the frame-sizing/layout math first, since that's what `--scale` most directly touches:

- `Resample.cpp::resampleToDisplay()` fits into `(maxW/scale, maxH/scale)` then magnifies by
  replicating pixels into `scale x scale` blocks. Its doc comment claims up to `(scale-1)` pixels of
  overflow past `(maxW, maxH)`, but `floor(maxH/s)*s <= maxH` shows that bound is never actually
  reached — not the source of a visible bug.
- `FrameBuilder.cpp` scales the inter-pane gap by `scale`, but sizes the output frame to what the
  resampler actually produced, not blindly to the box.
- `FramePlan.cpp::planFrame()` passes `boxWidth`/`boxHeight`/`scale` straight through with no extra
  scale-driven multiplication of the box itself.
- `Overlay.cpp::planOverlay()`'s footer-marker row already ceiling-divides on the *actual*
  `imageHeight` parameter, so it already adapts to a taller, scaled image.
- `Layout.cpp::computeGrid()`/`splitAxis()` gives the single-cell case (the common one) a budget
  that's exactly `boxW`/`boxH`.

So the image reaching the terminal is correctly bounded by the pixel box `Screen::pixelGeometry()`
computed during the session. The bug is in what happens to that already-correct image at exit time,
in the same retained-frame reprint (`Run.cpp:304-322`) touched by the previous entry:

```cpp
Terminal terminal;
if (terminal.initCli(stdout) && terminal.hasPixelSupport())
{
    for (const auto& line : lastHeader)
        terminal.printLine(line);
    terminal.blit(lastFrame.pixels.data(), lastFrame.width, lastFrame.height);
}
```

This prints at "the current cursor position" — wherever that is once `notcurses_stop()` has already
restored the primary screen. Leaving the alternate screen (`rmcup`/DEC 1049) conventionally restores
the cursor to wherever it was *before* the alternate screen was entered — i.e. wherever the shell
prompt was when `mriv` was launched, not row 0. `Terminal::blit()`'s `ncdirectf_render()`/
`ncdirect_raster_frame()` place the image at that cursor position and advance past it; per
`<notcurses/direct.h>`'s own doc comment, "the image may be arbitrarily many rows -- the output will
scroll" if there isn't enough room before the bottom edge.

At `scale==1`, most real MRI volumes render comfortably under the pixel box (the resampler's "never
upscale past native size" cap keeps them there), leaving vertical margin that happens to absorb a
non-zero starting row. At `scale>=2` that cap is far more likely to bind — the volume is fit into a
*smaller* box (`maxH/scale`) and magnified back up by nearest-neighbour replication until it nearly
fills the box again. A near-full-height image, printed from a cursor row left over from wherever the
shell prompt was, doesn't have enough rows left before the terminal's bottom edge and scrolls,
dragging the just-printed header text down with it and overlapping whatever's below — invisible at
`scale==1` (margin absorbs it), visible at `scale>=2` (no margin left).

Confirmed via notcurses' own source (`dankamongmen/notcurses`, fetched directly):

- `src/lib/direct.c`, `ncdirect_cursor_move_yx(n, y, x)`: when `y` and `x` are both `>= 0` it emits
  `ESCAPE_CUP` — terminfo's `cup`, **absolute** positioning, not a relative move. So
  `ncdirect_cursor_move_yx(nc, 0, 0)` moves the cursor to the literal top-left corner without touching
  existing screen content.
- `src/lib/termdesc.h`: `ESCAPE_CLEAR, // "clear" clear screen and home cursor` — confirms
  `ncdirect_clear()` also homes the cursor, but additionally blanks the whole screen, a bigger,
  unrequested side effect (it would erase pre-existing primary-screen content outside the printed
  area). `ncdirect_cursor_move_yx(nc, 0, 0)` is the minimal fix for the actual defect (not enough
  room, not "the screen looks wrong").

**Fix:** added `Terminal::moveCursorHome()` (`render/Terminal.hpp`/`.cpp`), wrapping
`ncdirect_cursor_move_yx(nc_, 0, 0)` in real mode and writing a fixed `"\x1b[H"` marker in test mode so
tests can pin call order against `blit()`. `runInteractive()`'s retained-frame reprint now calls it
before printing the header or blitting, best-effort like the rest of that block (existing code doesn't
gate on `printLine()`/`blit()`'s return values either). Homing to row 0 guarantees a full terminal
height of room below the cursor — the same amount `Screen::pixelGeometry()` already assumed was
available when it decided how tall `lastFrame` was allowed to be during the session — so starting the
reprint from row 0 always leaves enough room, regardless of `--scale`.

New coverage in `mriv_test_terminal`: `testMoveCursorHomeBeforeInitFails()`,
`testMoveCursorHomeStructure()` (mirroring the existing `blit()` tests), and
`testMoveCursorHomePrecedesBlitInTestMode()`, which is the property that actually matters here —
asserts the `"\x1b[H"` marker precedes `blit()`'s encoded bytes in a test-mode `Terminal`'s injected
stream. `ctest` green 47/47. **Needs the user to confirm on a real terminal, at both `--scale 1` and
`--scale 2`/`3`**, that quitting leaves exactly one picture on screen with no leftover image or text
overlapping it.
