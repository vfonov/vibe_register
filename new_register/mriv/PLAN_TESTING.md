# PLAN_TESTING.md

Testing plan for the terminal-based medical-image slice viewer. Scope: **only tests that are cheap, deterministic, and CI-friendly.** No Xvfb, no real terminal emulator, no headless Kitty, no visual snapshotting through a windowing system. Those exist as manual smoke tests and are out of scope for this document.

Read `PLAN.md` for the project spec and `CLAUDE.md` for the TDD workflow this testing plan assumes.

## What we test, and what we don't

The rendering pipeline for one slice looks like this:

```
parent API                    this subproject                       terminal
──────────                    ───────────────                       ────────
open_volume ─► read_slice ─► window/level ─► resample ─► encode ─► [bytes]  ─► emulator ─► pixels
                             (parent)         (ours)    (notcurses)    stdout       (out of scope)
```

**In scope for this document:** everything up to and including the escape-sequence bytes on stdout. That's what our code produces. If the bytes are structurally correct and decode to the right pixels, we've done our job.

**Out of scope:** whether a real Kitty/Ghostty/iTerm2 emulator, on a real display, actually paints those pixels correctly. That's the emulator's job. We verify it by hand during releases; we don't try to automate it here.

**Also out of scope:** re-testing the parent project's volume readers, DICOM traversal, or window/level implementation. Those have their own tests in the parent. We test *this subproject's consumption* of those APIs, not the APIs themselves.

## The three test layers we actually use

### Layer A — Escape-sequence structure (fast, ~ms per test)

Assert that our program emits the right *shape* of output: correct protocol, correct dimensions in the header, correct number of images, correct positioning between them. Bytes are captured to an in-memory buffer; no terminal, no I/O, no external processes.

This layer catches: wrong protocol selected, wrong image size sent, image sent zero or two times, missing cursor-position sequences between images in a strip, malformed escape framing.

**Most of your tests live here.** Aim for a dozen or two, each running in single-digit milliseconds.

### Layer B — Decoded-pixel correctness (medium, ~tens of ms per test)

Take the emitted bytes, parse the escape sequence, decode the payload back into an RGBA buffer, and compare against a reference. This catches "the framing was right but the pixels inside were wrong" — bad window/level application, resampler bugs, endianness mistakes, off-by-one in the row stride.

Cheaper than a real terminal because it's pure computation: base64 decoding, PNG decoding (for Kitty `f=100`), sixel decoding (for sixel output). All doable in-process.

**A handful of these per format is enough** — one golden per protocol per axis for a representative synthetic volume is a good baseline.

### Layer C — CLI-level integration (fast, ~tens of ms per test)

Invoke the binary as a subprocess (or, better, call `main()` directly if it's factored to accept `argc/argv` and streams), pass real-looking arguments, and assert on the combined stdout/stderr. Verifies argument parsing, mode dispatch, exit codes, error messages, and the whole pipeline glued together.

**A dozen of these covers the CLI surface.** Each is fast because the input volumes are tiny synthetics.

Nothing else. If a proposed test doesn't fit A, B, or C, either it belongs to the parent project's suite or it's a Layer 3 smoke test to run by hand.

## Prerequisites: seams that make cheap testing possible

Before writing tests, three small design decisions in the production code make everything downstream easy. Enforce these; they're worth it.

### 1. The `Terminal` class writes to an injectable stream

```cpp
class Terminal {
public:
    // Production ctor: writes to stdout, owns notcurses lifecycle.
    Terminal();

    // Test ctor: writes to the given stream, skips notcurses init.
    // The stream must outlive the Terminal.
    explicit Terminal(std::ostream& out, PixelProtocol forced);

    // ...
};
```

Notcurses's own initialization probes the real terminal (writes escape sequences, reads responses) — it doesn't work against an `ostringstream`. The test constructor bypasses notcurses entirely and drives our own encoders directly. Which brings us to:

### 2. The encoder is a pure function, separate from `notcurses_init()`

Factor slice encoding into a free function that takes a pixel buffer and returns bytes:

```cpp
// In src/render/encode.hpp — no notcurses handle required.
std::string encode_kitty_png(std::span<const std::uint8_t> rgba,
                             std::size_t width, std::size_t height);

std::string encode_sixel(std::span<const std::uint8_t> rgba,
                         std::size_t width, std::size_t height);
```

The production `Terminal` class calls these to build the payload, then hands it to notcurses (or writes it directly, if we choose to bypass notcurses's higher-level `ncvisual_*` API for encoding). Tests call these directly with a synthetic buffer — no `notcurses_init`, no environment probing, no flakiness.

If notcurses's encoder is doing the work under the hood, wrap the "hand notcurses a buffer, capture its output bytes" step behind the same signature so tests don't care which encoder ran.

### 3. `main()` is testable

```cpp
// In src/main.cpp — real entry point is one line.
int main(int argc, char** argv) {
    return mriv::term::run(argc, argv, std::cin, std::cout, std::cerr);
}

// In src/cli/run.hpp — this is what tests call.
namespace mriv::term {
int run(int argc, char** argv, std::istream& in, std::ostream& out, std::ostream& err);
}
```

Layer C tests call `run()` directly with argument arrays and captured streams. No subprocess spawning, no PATH concerns, no CI environment differences.

Do these three things and everything below becomes straightforward.

## Test fixtures: tiny, synthetic, deterministic

Do not commit real medical data. Generate fixtures at test setup time from code so they're reproducible, small, and free of licensing worries.

- **Synthetic volumes.** A helper `make_test_volume(dims, dtype, pattern)` that produces a small in-memory volume matching the parent's volume type, or writes a tiny `.nii` to a temp path. Patterns: gradient (X, Y, or Z-varying intensity), checkerboard, single bright voxel, uniform. Each pattern lets specific assertions be sharp — a Z-gradient makes "did we slice along the right axis" trivially testable.
- **Synthetic DICOM.** For DICOM, the parent likely has fixture helpers already; use them. If not, a hand-built DICOM stub (a few dozen bytes of a minimal Part-10 file, or a directory with 3–5 tiny single-frame instances) is enough. Do not depend on a full DCMTK-generated fixture for unit tests — too heavy.
- **Sizes.** Aim for 32×32×32 or 64×64×64. Big enough to exercise multi-slice logic, small enough that decoding and comparison are instant.
- **Location.** `tests/fixtures/` for generators, `tests/data/` for any small binary fixtures that must exist on disk. Keep the on-disk ones under 100 KB each.

## Layer A: escape-sequence structure tests

### The parser helper

Write a small parser once, reuse it everywhere. It walks a byte buffer and produces a `std::vector<EscapeEvent>`:

```cpp
enum class EventKind { KittyGraphics, Sixel, ITerm2Image, CursorMove, Other };

struct EscapeEvent {
    EventKind kind;
    std::map<std::string, std::string> params;  // parsed key=value for Kitty, etc.
    std::size_t payload_size;                    // bytes between introducer and terminator
    std::size_t offset;                          // where in the stream this event started
};

std::vector<EscapeEvent> parse_escape_stream(std::string_view bytes);
```

Implementation notes:

- Recognize introducers: `\x1bP...q` (sixel start), `\x1b_G` (Kitty), `\x1b]1337;` (iTerm2), `\x1b[<row>;<col>H` (cursor move).
- Recognize terminators: `\x1b\\` (String Terminator, ends sixel/Kitty/iTerm2 payloads).
- For Kitty, parse the `a=T,f=100,s=512,v=512;` header before the `;` into the params map.
- For sixel, parse the raster attributes `"1;1;W;H` after `Pq`.
- Don't try to be a full VT parser; be a *good-enough* parser for the escape kinds our app emits. A hundred lines of C++ is sufficient.

This parser is itself testable (Layer A test for the parser: "given this hand-crafted byte string, expect these events"). Get it right once, then all downstream tests read cleanly.

### Suggested Layer A tests

Each test forces a specific pixel protocol via env var or the test constructor, invokes the render path against a synthetic slice, captures bytes, parses, and asserts.

1. **Kitty protocol selected when configured.** With `NCPIXEL_IMPL=kittyanim`, output contains exactly one `KittyGraphics` event.
2. **Sixel protocol selected when configured.** With `NCPIXEL_IMPL=sixel`, output contains exactly one `Sixel` event, zero `KittyGraphics` events.
3. **Image dimensions match `--max-width` cap.** With `--max-width=200` on a 512×512 slice, the parsed Kitty `s` param equals 200 (or the aspect-preserving computed height/width).
4. **One image per input file in strip mode.** `mriv a.nii b.nii c.nii` produces exactly three image events. Also assert that cursor-move events separate them (structure of the strip layout).
5. **`--info` produces no image events.** Only text on stdout; parsed event list contains zero image events of any kind.
6. **`MRIV_REQUIRE_PIXELS` on no-pixel-support exits nonzero.** Simulate by forcing `NCPIXEL_IMPL=none` (or the equivalent notcurses knob) with the debug env var set; assert exit code and empty image event list.
7. **No pixel support in one-shot mode without `MRIV_REQUIRE_PIXELS` exits zero with nothing drawn.** Same forcing, without the env var; assert no image events but stderr contains a hint message. (No block-character fallback exists -- this was aspirational and was never implemented.)
8. **Empty payload never happens.** For every image event, `payload_size > 0`. Catches "we shipped a header with no data" bugs.
9. **All escape sequences are terminated.** The parser reports no dangling introducer without a matching terminator. Catches truncation bugs.
10. **Multiple slices from `--interactive` sequence.** Feed a scripted key sequence (`j`, `j`, `q`) via the injected stdin; assert three image events, one per slice change.

These ten cover most of the render-path surface without ever touching a real terminal.

## Layer B: decoded-pixel correctness tests

### Decoders

You need three small helpers:

- **Base64 decoder.** Ten lines. Or steal a permissively-licensed single-header one.
- **PNG decoder.** For Kitty `f=100` payloads. Use **stb_image.h** (single header, public domain) — it decodes PNG to RGBA in one function call. Add it under `tests/third_party/` so it doesn't touch the production dependency list.
- **Sixel decoder.** libsixel's `sixel_decoder_*` API. If libsixel isn't already available as a build dep, gate sixel decode tests behind a CMake option; run only Kitty decode tests otherwise. The Kitty tests alone give you good pixel-level coverage.

Wrap them:

```cpp
// tests/decode/decode_helpers.hpp
struct DecodedImage {
    std::vector<std::uint8_t> rgba;   // row-major, width*height*4
    std::size_t width;
    std::size_t height;
};

DecodedImage decode_kitty_event(const EscapeEvent& e);
DecodedImage decode_sixel_event(const EscapeEvent& e);   // optional
```

### Comparison strategies

Pick per test based on what you actually care about:

- **Property assertions** (most robust, use freely): dimensions match, mean pixel value is within a range, the image is not all-zero, a specific region is brighter than another. These survive resampler tweaks without needing golden regeneration.
- **Golden-image RMS comparison** (use sparingly, for regression insurance): compare decoded RGBA against a committed reference PNG within an RMS threshold (something like 2/255 average, tune to taste). Include a `regenerate_goldens.sh` script so intentional changes are one command away.
- **Structural comparison** (use for slice-orientation tests): compute a simple hash or signature of the decoded image and assert equality across runs. Great for "does the same slice always produce the same output" without pinning exact pixels.

Do not do full SSIM or perceptual metrics in the standard test suite. Property assertions + a small number of goldens is enough; adding SSIM adds a dependency and a slow test for marginal signal.

### Suggested Layer B tests

1. **Z-gradient volume, axial mid-slice, all pixels in decoded image are equal.** A Z-gradient volume's middle axial slice is a uniform intensity plane. If the decoded image isn't uniform (± 1 for rounding), either the axis is wrong or the resampler is broken.
2. **X-gradient volume, sagittal mid-slice, decoded image has a horizontal gradient.** Assert that mean intensity of the left column is measurably lower than the right column (or vice versa, per convention). Catches axis-swap bugs.
3. **Checkerboard volume at native display size decodes to a checkerboard.** With `--max-width` set so no resampling happens, count transitions across a row and assert it matches the checkerboard period. Catches resampler-off-by-one and byte-order bugs.
4. **Auto window/level produces non-degenerate output.** Uniform-intensity slice with `--auto-window` still produces a valid (if flat) image, not all-black or all-white. Assert that the decoded image mean is not 0 and not 255.
5. **Explicit `--range` clips as expected.** A gradient slice with a narrow `--range` should decode to an image that's mostly saturated at both ends and has a small transition band. Assert histogram shape: > X% of pixels are near 0, > Y% near 255, small fraction in the middle.
6. **Golden Kitty PNG for one canonical fixture.** RMS-compare decoded output of `mriv fixture_axial.nii` against `golden/axial_kitty.png` within threshold.
7. **Golden sixel for the same fixture (if sixel decoder is available).** Same as above with `NCPIXEL_IMPL=sixel`.

Seven or so tests here is plenty. Add more only when a specific bug demands one.

## Layer C: CLI integration tests

Invoke `mriv::term::run(argc, argv, in, out, err)` directly. Use a small `Args` helper to build `argv`:

```cpp
struct Args {
    std::vector<std::string> storage;
    std::vector<char*>       argv;
    Args(std::initializer_list<std::string> a);
    int argc() const;
    char** data();
};
```

Then:

```cpp
std::ostringstream out, err;
std::istringstream in;
int rc = mriv::term::run(Args{"mriv", "--info", "fixture.nii"}.argc(), ..., in, out, err);
CHECK(rc == 0);
CHECK(out.str().find("dims: 64 64 64") != std::string::npos);
```

### Suggested Layer C tests

1. **`--help` prints usage and exits 0.** Basic sanity that argument parsing is wired up.
2. **`--version` prints a version string and exits 0.**
3. **Unknown flag exits nonzero with a diagnostic on stderr.**
4. **`--info fixture.nii` prints metadata to stdout, no image bytes.** Combine with a Layer A parse to confirm zero image events.
5. **Nonexistent input file exits nonzero with a clear error on stderr.**
6. **`--slice 999` (out of range) exits nonzero with a clear error.**
7. **`--slice mid` selects the middle index.** Combine with Layer B: decoded image matches the golden for the middle slice.
8. **`--slice 50%` on a 64-slice volume selects slice 32.**
9. **`--axis y` produces a different image than `--axis z` on the same volume.** Layer B property assertion: decoded images differ meaningfully.
10. **Multiple positional inputs produce a strip.** Layer A parse: N image events for N inputs.
11. **`--list-series` on a synthetic DICOM directory prints the series table and exits 0, no images.**
12. **`--series 1 dicom_dir/` renders the selected series' middle slice.** Layer A: one image event.

Twelve or so tests cover the whole CLI. Add one whenever a new flag or mode is added.

## What to explicitly *not* test in this suite

- **Notcurses's terminal detection logic.** That's notcurses's job. We test that we honor `NCPIXEL_IMPL` overrides and that our code responds correctly to "no pixel support"; we do not test the full auto-detection matrix.
- **The parent's volume readers.** If a `.nii` file fails to open with a valid input, that's a parent bug — file it there. Our tests use synthetic fixtures that are known to work with the parent's readers.
- **Real-terminal visual correctness.** Save for manual smoke tests. Add a `smoke/README.md` listing the terminals to eyeball before a release (Kitty, Ghostty, WezTerm, iTerm2, Konsole, at minimum).
- **Performance.** No microbenchmarks in this suite. If slice extraction is slow, that's usually a parent-side issue and belongs in the parent's benchmarks.

## Framework and CMake wiring

- **doctest** as specified in `PLAN.md`. Fetched via CMake.
- Test targets prefixed `mriv_test_*` so parent's `ctest` can filter them.
- One target per layer is a reasonable default: `mriv_test_structure` (Layer A), `mriv_test_pixels` (Layer B), `mriv_test_cli` (Layer C). Or one target per source file — either works.
- No env-var gates on standard tests. They must all run in a plain `ctest` invocation with no setup beyond `cmake --build`.
- The one exception: sixel decode tests can be gated on a CMake option `MRIV_TESTS_WITH_LIBSIXEL=ON` if libsixel isn't a hard dep. Kitty decode tests are unconditional.

## Determinism checklist

Before landing any test, verify:

- [ ] Runs identically on Linux and macOS (no path separators, no locale-dependent formatting, no `/tmp` assumptions — use `std::filesystem::temp_directory_path()`).
- [ ] Does not depend on `TERM`, `COLORTERM`, or any inherited env var. Set what you need explicitly.
- [ ] Does not depend on network, wall-clock time, or filesystem state outside the test's own temp dir.
- [ ] Cleans up any temp files it creates (use RAII helpers or `std::filesystem::remove_all` in test teardown).
- [ ] Completes in under 100 ms on a laptop. If a test is slower, either shrink the fixture or split it.

## Running the suite

From the parent's build tree:

```bash
ctest --test-dir build --output-on-failure -R "^mriv_test_"
```

From this subproject's standalone build:

```bash
ctest --test-dir build --output-on-failure
```

Both should be clean before any commit lands on `main`. See `CLAUDE.md` for the TDD loop this suite is built around.
