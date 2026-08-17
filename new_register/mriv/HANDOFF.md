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
| M4 | Multi-file strip, `--max-width`, `--require-pixels`, no-pixel fallback | Outline — [§7](#7-m4m6--outline) |
| M5 | Interactive TTY mode | Outline — [§7](#7-m4m6--outline) |
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
