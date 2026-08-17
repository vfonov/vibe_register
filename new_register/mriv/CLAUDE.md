# CLAUDE.md

Guidance for Claude Code working in this subproject. **Read `PLAN.md` for the full specification** — this file only covers *how* to work, not *what* to build.

## Context: this is a subproject

This directory is `new_register/mriv/`, a subproject of `new_register/` — a modern C++17 rewrite of the BIC `register` tool (Vulkan + ImGui + MINC2) — referred to below as the "parent project". The parent already has working, tested code for:

- **Reading medical image volumes** — MINC2 (`.mnc`) via minc2-simple, and NIfTI (`.nii`, `.nii.gz`) via a vendored niftilib. One entry point: `Volume::load()`.
- **Extracting 2D slices** and rendering them to a **finished RGBA buffer** — `renderSlice()` in `include/SliceRenderer.h`.
- **Intensity mapping** — a `valueMin`/`valueMax` range model, plus `Volume::computeQuantile()` for percentile-based defaults.
- **Colour maps** — 18 of them, `enum class ColourMapType`.
- **Volume metadata** — dimensions, voxel step, start, direction cosines, world↔voxel transforms.

All of this lives in the **`nr_core`** static library target (`new_register/CMakeLists.txt:429`), which is deliberately GPU-free so headless tools can link it. `new_mincpik` is an existing headless consumer and the closest working model for what this subproject should look like.

The parent notably does **not** have DICOM support or 4D/time-series volumes. Those gaps are documented in `PLAN.md`'s deferred-work section; they are not ours to fill.

**This subproject reuses the parent's components.** It adds only what the parent doesn't have: a terminal-based rendering path (notcurses) and a scriptable CLI (cxxopts). Everything else — image I/O, slice extraction, intensity mapping, colour mapping — comes from `nr_core`.

The rule to internalize: **if you find yourself writing MINC2 or NIfTI parsing, slice extraction, intensity windowing, or colour-map code in this subproject, stop.** That code exists in the parent. Find it, understand it, use it. If the parent's API doesn't fit, the fix is (in order of preference) (a) call it differently, (b) add a small adapter in this subproject, or (c) extend the parent's API with a minimal, backward-compatible addition. Never fork or reimplement.

## Before you touch anything

1. Read `PLAN.md` end-to-end. It is the source of truth for scope, architecture, and the boundary between this subproject and the parent. Its "What the parent already provides" section is a complete API inventory — start there before opening any parent header.
2. Read the repository's `AGENTS.md` (at the repo root), then these headers, in this order:
   - `new_register/include/Volume.h` — loading, metadata, `computeQuantile()`, `slicePixelAspect()`
   - `new_register/include/SliceRenderer.h` — `VolumeRenderParams`, `RenderedSlice`, `renderSlice()`
   - `new_register/include/ColourMap.h` — `ColourMapType`
   - `new_register/include/NiftiVolume.h` — for context on how `Volume::load()` dispatches
   Also skim `new_register/src/mincpik/` — it is the parent's other headless `nr_core` consumer and shows the intended usage pattern.
3. Skim the existing code in this subproject for the module you're about to change.
4. If the task is ambiguous, or seems to conflict with `PLAN.md`, or would require duplicating parent-project code, ask before writing code. Do not silently reinterpret the spec or reinvent existing components.

## How to work: red-green-refactor TDD

**All new behavior arrives via test-first development.** No exceptions for "small" changes — small changes are exactly where regressions hide. The loop for every feature, bugfix, or new capability:

1. **Red.** Write a failing test that expresses the desired behavior. Run it. Confirm it fails *for the reason you expect* (a wrong-value assertion, not a compile error or a missing symbol — those mean the test itself is wrong).
2. **Green.** Write the minimum code that makes the test pass. Resist the urge to add "while I'm here" improvements. Ugly is fine at this step.
3. **Refactor.** With the test green, clean up: extract helpers, rename, tighten types, remove duplication. Run the test again after every non-trivial edit. If it goes red, the refactor is wrong — revert, don't debug forward.

Commit at the end of each green step (or each green-plus-refactor cycle). Small commits with a passing test suite at every point in history are the goal. A commit message like `render: resample slice to display width before blit` beats `wip` every time.

### What "a test" means here

- **Consumer tests** for how this subproject uses parent APIs — use real parent types where they're cheap (`Volume::generate_test_data()` and the tiny `new_register/tests/sq1.mnc` make this easy), fake them where they're expensive. The goal is to test *this subproject's* logic, not re-test the parent's.
- **Unit tests** for the code that genuinely lives here: the notcurses wrapper, CLI parsing, the resampler-to-terminal-cells math (including the `slicePixelAspect()` correction), the `--axis` → `viewIndex` mapping.
- **Integration tests** using tiny sample volumes that exercise the full path from CLI invocation to encoded output. Capture the escape-sequence output to a buffer and assert on structure, not exact bytes.
- Rendering tests that require a real terminal are gated on `MRIV_TEST_RENDER=1` and skipped by default.
- **Framework: the parent's.** Plain `assert` in a `main()`, built with the `add_nr_test()` macro in `new_register/tests/CMakeLists.txt:25` and registered via `add_test(NAME mriv_...)`. Do not add doctest, GoogleTest, Catch2, or anything else — the parent has none, and its 26 tests get along fine without one.
- **No absolute paths in test source.** Pass data paths as command-line arguments from CMake (`AGENTS.md` §7.9).

### When TDD is genuinely awkward

Two situations where the "test first" rule bends:

- **Spikes / exploration** — including exploring how the parent's API works. If you don't yet know what your consumer code should look like, throw code at the wall on a scratch branch to learn. Then *delete it*, start over on the real branch, and write the tests first. Do not merge spike code.
- **Pure refactoring** (no behavior change). The existing tests are the safety net. If coverage is thin in the area you're refactoring, *add tests first* to pin current behavior, then refactor.

Bugfixes are always TDD: write a failing test that reproduces the bug, fix it, watch it go green.

## Coding conventions

Match the parent (`AGENTS.md` §6):

- **C++17** — `CMAKE_CXX_STANDARD 17`. Do not reach for C++20 features. (The parent has a documented, unscheduled ambition to move to C++23; when that lands we follow, we don't lead.)
- `PascalCase` types and classes, `camelCase` methods and variables.
- Allman braces (opening brace on its own line), 4-space indent, no tabs.
- Errors to `std::cerr` — never `printf` to stderr.
- Include order: system → library → local.
- Namespace this subproject under `mriv::term`. No clash — the parent uses only `QC::`.
- RAII everywhere. No raw `new`/`delete`.
- **No exceptions across the C-library boundary (notcurses).** The parent has no `Result<T, E>` or `Expected<T, E>` type, so don't hunt for one: wrap notcurses C return codes in `std::optional`, a small local status enum, or a `bool` + `std::cerr` message, whichever fits. Note that the parent's own `Volume::load()` *does* throw `std::runtime_error` — catch it at the CLI boundary and turn it into a clean message plus a non-zero exit.
- Header hygiene: forward-declare where possible. This subproject's headers must not force notcurses includes on the parent — keep `<notcurses/notcurses.h>` inside `.cpp` files or clearly-marked internal headers.
- **Notcurses:** use the C API directly (`<notcurses/notcurses.h>`). The C++ bindings package (`libnotcurses++-dev`, headers under `<ncpp/...>`) may be installed for reference, but this subproject does not use them — their exception-based error model conflicts with the project's error handling, and our own `render/terminal.cpp` wrapper already provides the RAII and typing we need. Do not reach for `ncpp::` reflexively.
- Warnings-as-errors in Debug builds. If a warning is genuinely spurious, silence it locally with a pragma and a comment explaining why.

### One deliberate divergence from the parent

This subproject uses **cxxopts** for CLI parsing. The parent removed cxxopts and hand-rolls its argv walking (`new_register/src/main.cpp:49`, `src/mincpik/mincpik_cli.cpp`). That difference is an explicit decision, recorded in `PLAN.md`. If you notice the inconsistency, leave it alone — "aligning with the parent" here would be undoing a choice, not fixing a mistake.

## Change hygiene

- **Small, reviewable diffs.** A commit that touches one CLI flag or one rendering step is far easier to reason about than a sweeping refactor. If a change wants to grow, split it.
- **One concern per commit.** Formatting churn, dependency bumps, and behavior changes go in separate commits.
- **Never modify parent-project code from this subproject's commits** unless the task is explicitly to extend a parent API. The one expected parent-side edit — adding `option(BUILD_TERMINAL_VIEWER)` and `add_subdirectory(mriv)` to `new_register/CMakeLists.txt` — is itself a separate commit. If you need something else from the parent, propose it as a separate commit (or ideally a separate PR), then update this subproject to consume it.
- **Never touch `legacy/`.** It is read-only reference material for the whole repository (`AGENTS.md` §7.2).
- **New feature?** Update `README.md`'s feature list (one line) in the same commit. If it changes the CLI surface, update the help text and any CLI examples in `PLAN.md`.
- **Dependency added?** This should be extremely rare. This subproject's whole point is to add minimal surface area over `nr_core`. If you genuinely need a new dependency, justify it in the commit message and update `PLAN.md`'s dependency list.

## Running the loop

This subproject builds as part of the parent's tree via `add_subdirectory()`. The parent's build root is `new_register/CMakeLists.txt` — there is no top-level `/app/CMakeLists.txt`. Typical dev cycle:

```bash
cd new_register/build
cmake ..
make -j$(nproc)
ctest --output-on-failure -R "^mriv_"   # our tests are prefixed
```

If the subproject supports standalone builds (see `PLAN.md`), the loop from *this* directory:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMRIV_STANDALONE=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Standalone mode still needs a pre-built `nr_core` and its MINC/HDF5 stack — it skips the ImGui/Vulkan build cost, not the parent dependency.

Fast inner loop while iterating on one test:

```bash
cmake --build . -j --target <test_target>
./tests/<test_target>
```

Sample volumes for smoke testing live in `test_data/` at the repo root and in `new_register/tests/` — reuse those rather than committing new ones.

## Things not to do

- Do not reimplement volume reading, slice extraction, intensity windowing, colour mapping, or metadata parsing. `nr_core` provides all of these.
- Do not add niftilib, DCMTK, GDCM, MINC, or HDF5 to this subproject's link list. Link `nr_core`; it brings them transitively.
- Do not replace cxxopts with a hand-rolled parser to "match the parent." See the divergence note above.
- Do not add doctest, GoogleTest, or Catch2. Use `add_nr_test()` and plain `assert`.
- Do not modify parent-project source from commits in this subproject unless explicitly tasked to.
- Do not add a GUI here. Not Qt, not ImGui, not "just for previewing." The parent has ImGui; this subproject is the terminal path.
- Do not swap notcurses for FTXUI, ncurses, or hand-rolled escape sequences.
- Do not use the `ncpp::` C++ bindings for notcurses even though `libnotcurses++-dev` may be installed; use the C API directly.
- **Do not build `--series`, `--list-series`, or `-t`/`--time`.** The parent has no DICOM reader and no 4D volume model, so there is nothing to call. See `PLAN.md`'s "Deferred work — blocked on the parent" section.
- Do not skip the `Volume::slicePixelAspect()` correction in the resampler. Anisotropic voxels are the common case; without it, most real volumes render stretched.
- Do not implement caching, prefetching, or interactivity before the non-interactive path is end-to-end green.
- Do not skip the failing-test step because "it's obviously going to fail." Run it. The one time you skip is the time the test was wrong.
- Do not merge with a red test suite. If a test is legitimately obsolete, delete it (in its own commit) with a note explaining why.
- Do not "helpfully" extend platform support to Windows in v1.

## When you're stuck

- Re-read the relevant section of `PLAN.md`. The answer is often there.
- Check whether `nr_core` already provides what you're about to build. It probably does — the inventory in `PLAN.md` lists the exact signatures.
- Look at `new_register/src/mincpik/` for a worked example of a headless `nr_core` consumer.
- If the parent's API doesn't quite fit, describe the mismatch in a design note before adding a workaround. A small, thoughtful parent-API extension is usually better than an awkward adapter here.
- If the spec is silent or contradictory, ask. Don't guess and don't invent scope.
