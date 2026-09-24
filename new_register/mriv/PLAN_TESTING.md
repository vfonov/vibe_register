# PLAN_TESTING.md

How mriv is tested. For the TDD loop, see `CLAUDE.md`; for traps and the pending manual checks,
see `HANDOFF.md`.

**Scope:** only tests that are cheap, deterministic and CI-friendly. There is no real terminal
emulator, no Xvfb and no screenshotting of a window. Anything that depends on what a real
emulator actually paints is checked by hand (`HANDOFF.md` §4).

We also don't re-test the parent's readers, `renderSlice()` or the colour maps. We test how mriv
*consumes* them.

## Framework

- Tests are plain `assert` in a `main()`, registered with `add_mriv_test(<name> [ARGS ...])` in
  `tests/CMakeLists.txt`. The CTest name is `mriv_<name>`. There is no test framework.
- Fixture paths come from CMake as argv (`MRIV_TEST_DATA_DIR`, `new_register/tests/sq1.mnc`,
  `sq2.mnc`). A test that needs a fixture must `assert` that its argv is present, never skip
  silently (see `HANDOFF.md` §3).
- Otherwise, build synthetic volumes in memory with `Volume::generate_test_data()` or small local
  helpers.
- Run with `ctest --output-on-failure -R "^mriv_"` from `new_register/build`.

## Seams that make this possible

| Seam | Where | Purpose |
|------|-------|---------|
| `Terminal(std::ostream&, PixelProtocol)` | `src/render/Terminal.hpp` | Test-mode terminal. Skips notcurses and writes Kitty escapes to a stream |
| `encodeKittyPng()` / `encodePixels()` | `src/render/Encode.hpp` | Pure encoders, with no notcurses handle |
| `mriv::term::run(argc, argv, in, out, err)` | `src/cli/Run.hpp` | The whole CLI with injected streams; `main.cpp` is one line that calls it |
| `MRIV_TEST_RENDER` env var | `src/cli/Run.cpp` | The one-shot path uses the test-mode terminal. `=none` selects the no-pixel-support branch |
| `MRIV_REQUIRE_PIXELS` env var | `src/cli/Run.cpp` | Makes one-shot mode exit 1 when there is no pixel support; otherwise it exits 0 |
| `runSession()` with a `KeySource` / `FrameSink` | `src/interactive/Session.hpp` | Drives the interactive loop from a scripted key string |
| `parseEscapeStream()` | `tests/escapes.hpp` | Splits output into `KittyGraphics` / `CursorMove` / `Text` events |
| `decodeKittyEvent()` | `tests/decode.hpp` | base64 → PNG → RGBA, using stb (`mriv/third_party/stb/`) |

`interactive/Screen` (full notcurses) is the only part no test reaches. Keep it free of decisions.

## Layers

| Layer | What it asserts | Suite |
|-------|-----------------|-------|
| **A — escape structure** | Right protocol framing, image count, non-empty payloads, all sequences terminated | `mriv_test_structure` |
| **B — decoded pixels** | Decoded images have the right size and properties. Examples: an axial mid-slice of a Z-gradient is uniform; a sagittal Y-gradient decodes to a horizontal gradient; a checkerboard survives; auto-window output isn't degenerate; `--max-width` holds | `mriv_test_pixels` |
| **C — CLI integration** | Flags, exit codes, error messages and mode dispatch through `run()`. Examples: `--views`/`--axis`/`--slice`/`--range`/`--scale`; multi-file grids with captions; skipping missing files; no-pixel handling; the interactive-mode decision | `mriv_test_cli` |
| **Unit** | Pure modules | the other 20 `mriv_*` suites (`test_view_state`, `test_session`, `test_layout`, `test_overlay`, `test_resample`, …) |

Prefer property assertions (size, mean, "images differ", ordering) to golden images; there are
no goldens. Some behaviours that are easy to get wrong:

- `--slice` values outside the range clamp; they are not errors.
- `--axis` chooses which plane `--slice` and `j`/`k` apply to. It does not choose what is drawn;
  `--views` does.
- A single file is uncaptioned. Several files become the columns of one captioned image.

## Determinism checklist

- Must not depend on `TERM`, `COLORTERM` or other inherited environment. Set what the test needs.
- Temp files go under `std::filesystem::temp_directory_path()` and are removed afterwards.
- No network access and no wall-clock time.
- Must pass on Linux and macOS.
