# HANDOFF.md — mriv traps and pending checks

What `PLAN.md` (design, scope, status) and `CLAUDE.md` (how to work) don't say: traps that were
verified the hard way, lessons, and the real-terminal checks still owed. The step-by-step history
of how each was found is in `git log -- new_register/mriv`.

---

## 1. Build traps

- **`add_nr_test()` can be called from `mriv/`, but the variables it relies on come through empty.**
  The macro (`new_register/tests/CMakeLists.txt:25`) is global, but `SRC_DIR`, `COMMON_INCLUDES` and
  `TEST_DATA_DIR` are directory-scoped and are `""` here. It also accepts `ARGS` and **ignores it**.
  `add_mriv_test()` (`tests/CMakeLists.txt`) handles all of this: it passes includes explicitly,
  uses `MRIV_TEST_DATA_DIR`, and forwards `ARGS` to its own `add_test`. Always register tests
  through it; never pass `SOURCES` to `add_nr_test`.
- **`add_executable` target names are global to the whole CMake project.** A mriv test with the
  same name as a parent test fails to configure. That is why the file is called
  `test_mriv_volume_info.cpp`.
- **The parent puts `.a` first in `CMAKE_FIND_LIBRARY_SUFFIXES`, and every subdirectory inherits
  that.** Left as it is, the system notcurses static archive is picked up without its transitive
  deps, and `find_library(m|rt)` / tinfo resolve to glibc's static archives, which fails at link
  time with `__frexp`, `_dl_x86_cpu_features` and `DT_TEXTREL`. Both `mriv/CMakeLists.txt` and
  `cmake/BuildNotcurses.cmake` reset the order to `.so`-first in their own scope. Keep it that way.
- **`notcurses_init()` and `ncdirect_init()` are not in `notcurses-core`.** They are defined in
  `src/media/shim.c`, which upstream compiles into the full `notcurses` target. A self-built static
  link (`MRIV_BUILD_NOTCURSES=ON`) therefore needs `notcurses-static` as well as the core archive.
  With `USE_MULTIMEDIA=none` this only pulls in a stub backend.

## 2. API traps (parent and notcurses)

- **`ncvisual_from_rgba(rgba, rows, rowstride, cols)`**: the middle argument is the row stride
  **in bytes**, not the column count. For a packed `w × h` buffer, call
  `ncvisual_from_rgba(p, h, w * 4, w)`. A wrong value gives a skewed image, not an error.
- **`colourMapByName()` only matches exact display names** (`"Gray"`, `"Hot Metal"`, …; there is
  no `"grayscale"`). mriv normalises names in `cli/ColourMapArg.cpp`, so use that, not the parent
  lookup.
- **`viewIndex` geometry.** 0 = axial (w=x, h=y), 1 = sagittal (w=y, h=z), 2 = coronal (w=x, h=z).
  The `slicePixelAspect()` axis pairs are `(0,1)`, `(1,2)` and `(0,2)`; there is no formula for
  them, so they live in a table in `render/SliceGeometry`. If you swap two of them, anisotropic
  data comes out plausibly but wrongly stretched. `--axis x|y|z` maps to `1|2|0`.
- **`renderSlice()` rows are already flipped to top-down.** Don't flip them again.
- **`renderSlice()` returns an empty result (0×0) for an empty volume** rather than throwing. Check
  the size before blitting.
- **`Volume::load()` throws `std::runtime_error`.** Catch it at the CLI boundary (`cli/Run.cpp`).
- **`notcurses_refresh()` clears the screen**, and with it any placed Kitty-protocol image.
  Forcing it after each frame fixed nothing on mlterm and blanked Ghostty (reverted in `3ac21b6`;
  see the comment in `Screen::drawFrame()`). Never land a pixel-protocol fix without testing it on
  both a sixel terminal (mlterm) and a Kitty-protocol one (Kitty or Ghostty).
- **Notcurses sends plane geometry and plane destruction to the terminal only at render time.**
  - `Screen::pixelGeometry()` renders before it reads, or the frame after a resize is built at
    the old size.
  - `Screen::destroy()` renders after destroying the image plane and before `notcurses_stop()`,
    or the old image can survive the switch back to the primary screen.
  - Use `notcurses_render()` for these, not `notcurses_refresh()`.
- **`notcurses_stop()` may clear bitmaps even with `NCOPTION_NO_CLEAR_BITMAPS` set.** This is why
  the one-shot path uses `ncdirect` (`render/Terminal`), and why the frame kept on screen after
  exit is re-blitted through it after homing the cursor (`Terminal::moveCursorHome()`).
- **`notcurses_init()` blocks on a bare pty** that doesn't answer its capability handshake, so
  `Screen` can't be driven from a `pty.fork()` harness either.

## 3. Test traps and lessons

- **The dev container can't display pixels** (`TERM=xterm`, `NCPIXEL_NONE`), and nothing here can
  run `Screen`.
  - Keep `Screen` free of decisions; logic belongs in the tested modules (`ViewState`, `Session`,
    `Overlay`, `FrameBuilder`, …).
  - Anything a user sees on screen needs a human check on a real terminal (§4).
- **A green `ctest` can be hollow.** `mriv_test_cli` once passed while skipping 9 of its 14 tests,
  because `ARGS` was dropped and the test silently skipped when it had no fixture.
  - Fixture-dependent tests now `assert` that their argv is present instead of skipping.
  - If a test runs much faster than its work should take (0.02 s for a test that "loads a
    volume"), treat that as evidence something is wrong.
- **Mutation-check new assertions.** Delete the branch under test and confirm the test fails.
  This has caught weak assertions more than once. For example, a `"terminal"` substring check
  passed via an unrelated error message.
- **`sq1.mnc` contains only the values `{0, 1}`.** A `--range 0,1` test gives the same output as
  auto-window. Use a range that falls strictly inside or straddles those values, such as
  `-1,2`. Strip tests should use `sq1.mnc` and `sq2.mnc` together, so the payloads actually differ.
- **In test mode the pixel box is a fixed 4096×4096**, and `computeResampleSize()` never upscales.
  Logic that only affects output when the box is small can't be observed through `MRIV_TEST_RENDER`
  with these fixtures.

## 4. Real-terminal checks still pending

No one has confirmed these on a real Kitty, Ghostty, WezTerm or iTerm2 session (with mlterm for
sixel where relevant):

1. **Quit at `--scale 1` and at `--scale 2`/`3`** (`d5e76c9`): exactly one picture should be left on
   screen, with no leftover image or overlapping text.
2. **`s` screenshot**: "Screenshot saved: screenshotNNNNNN.png" should appear on the status row,
   the file should land in the working directory, and the message should clear on the next key.
3. **Crosshair**:
   - The yellow crosshair should follow the cursor in every pane, including across volumes whose
     dimensions don't match.
   - Piped one-shot output should have no crosshair.
4. **Header layout**: file names, then status row, then hotkey row, then image, with no rows
   overlapping. This must hold both when idle and with the `r` range prompt open.

When one is confirmed, remove it from this list.
