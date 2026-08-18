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
