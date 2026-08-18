/// test_terminal.cpp — notcurses (direct-mode) wrapper lifecycle and blit
/// path.
///
/// The dev host has no pixel-capable terminal (HANDOFF.md sec 3.9), so
/// these tests redirect ncdirect at a tmpfile() and assert on structure
/// (did it crash, is output non-empty, does it contain an escape byte),
/// never on exact bytes -- terminal output isn't byte-stable across
/// notcurses versions. Despite <notcurses/direct.h>'s doc comment that its
/// FILE* "must be a tty", ncdirect_init() does not enforce this at runtime
/// (verified experimentally) -- tmpfile() works fine as a redirect target.

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <notcurses/direct.h>

#include "render/Terminal.hpp"

using namespace mriv::term;

namespace
{

// Matches the flags Terminal::initCli() uses in production.
constexpr uint64_t kTestFlags = NCDIRECT_OPTION_DRAIN_INPUT;

/// init()/destroy() must not crash, repeatedly, and via move too.
void testInitLifecycle()
{
    FILE* f = tmpfile();
    assert(f);

    {
        Terminal term;
        assert(term.init(f, kTestFlags));

        Terminal moved(std::move(term));
        assert(moved.hasPixelSupport() == moved.hasPixelSupport()); // no crash, stable
    }

    fclose(f);
}

/// A second init() on the same Terminal must tear down the first context
/// cleanly rather than leaking it.
void testReinit()
{
    FILE* f1 = tmpfile();
    FILE* f2 = tmpfile();
    assert(f1 && f2);

    {
        Terminal term;
        assert(term.init(f1, kTestFlags));
        assert(term.init(f2, kTestFlags));
    }

    fclose(f1);
    fclose(f2);
}

/// blit() before init() must fail cleanly, not crash.
void testBlitBeforeInitFails()
{
    Terminal term;
    std::vector<uint32_t> pixels(4, 0xFFFFFFFFu);
    assert(!term.blit(pixels.data(), 2, 2));
}

/// blit() with an empty image must fail cleanly.
void testBlitEmptyImageFails()
{
    FILE* f = tmpfile();
    assert(f);
    {
        Terminal term;
        assert(term.init(f, kTestFlags));
        std::vector<uint32_t> pixels;
        assert(!term.blit(pixels.data(), 0, 0));
    }
    fclose(f);
}

/// Attempt a real blit. Whatever the outcome (this sandbox has no pixel
/// protocol -- HANDOFF.md sec 3.9), the call must not crash. If it does
/// report success, the output must be structurally plausible: non-empty
/// and containing an ESC byte.
void testBlitStructure()
{
    FILE* f = tmpfile();
    assert(f);

    bool blitOk = false;
    {
        Terminal term;
        assert(term.init(f, kTestFlags));

        std::vector<uint32_t> pixels{0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFFFFFFFFu};
        blitOk = term.blit(pixels.data(), 2, 2);
    }

    if (blitOk)
    {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        assert(size > 0);

        rewind(f);
        std::vector<char> buf(static_cast<size_t>(size));
        size_t n = fread(buf.data(), 1, buf.size(), f);
        assert(n == buf.size());

        bool hasEsc = false;
        for (char c : buf)
        {
            if (c == '\x1b')
            {
                hasEsc = true;
                break;
            }
        }
        assert(hasEsc);
    }

    fclose(f);
}

/// Regression coverage for a real-world bug, in two rounds (see
/// HANDOFF.md): round 1 found that blit() left ncvisual_options::y/x at
/// their zero-initialized default via full notcurses's ncvisual_blit(),
/// placing the image at row 0 of the standard plane instead of the
/// current cursor position. Round 2 found that even after fixing
/// placement, notcurses_stop() could wipe the just-drawn bitmap on exit
/// per its own NCOPTION_NO_CLEAR_BITMAPS doc comment ("might still get
/// cleared even if this is set") -- confirmed against a real Kitty
/// session. Both were properties of full notcurses's whole-screen
/// teardown. The fix was to stop using that API for the one-shot render
/// path and switch to ncdirect, which owns neither a "screen" to restore
/// nor manual y/x placement: ncdirect_raster_frame() places the image at
/// the current cursor position and advances past it internally, and
/// ncdirect_stop() has no bitmap-clearing teardown to speak of. There is
/// no longer any cursor bookkeeping in this class for a test to pin. This
/// is a lighter smoke check that blit() still succeeds and does not
/// regress to leaving the cursor row unmoved when the query round-trip
/// can succeed -- ncdirect_cursor_yx() requires a real terminal reply and
/// fails (not hangs, verified experimentally) against a tmpfile(), so
/// `after > before` is unreachable in this sandbox exactly as it was
/// before the rewrite; it is exercised against a real terminal.
void testBlitSucceedsAndCursorAdvancesWhenQueryable()
{
    FILE* f = tmpfile();
    assert(f);

    unsigned before = 0, after = 0;
    bool blitOk = false;
    {
        Terminal term;
        assert(term.init(f, kTestFlags));
        before = term.cursorRow();

        std::vector<uint32_t> pixels{0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFFFFFFFFu};
        blitOk = term.blit(pixels.data(), 2, 2);
        after = term.cursorRow();
    }

    if (blitOk && after != 0)
        assert(after > before);

    fclose(f);
}

} // namespace

int main()
{
    testInitLifecycle();
    testReinit();
    testBlitBeforeInitFails();
    testBlitEmptyImageFails();
    testBlitStructure();
    testBlitSucceedsAndCursorAdvancesWhenQueryable();
    return 0;
}
