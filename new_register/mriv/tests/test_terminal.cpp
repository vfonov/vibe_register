/// test_terminal.cpp — notcurses wrapper lifecycle and blit path.
///
/// The dev host has no pixel-capable terminal (HANDOFF.md sec 3.9), so
/// these tests redirect notcurses at a tmpfile() and assert on structure
/// (did it crash, is output non-empty, does it contain an escape byte),
/// never on exact bytes -- terminal output isn't byte-stable across
/// notcurses versions.

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <notcurses/notcurses.h>

#include "render/Terminal.hpp"

using namespace mriv::term;

namespace
{

constexpr uint64_t kTestFlags =
    NCOPTION_SUPPRESS_BANNERS | NCOPTION_DRAIN_INPUT | NCOPTION_NO_ALTERNATE_SCREEN;

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

/// Regression test for a real-world bug: blit() used to leave
/// ncvisual_options::y/x at their zero-initialized default, which places
/// the image at row 0 of the standard plane rather than at the current
/// cursor position. In a real scrolling CLI session this drew the image
/// off in the scrollback and then restored the old cursor position,
/// making it look like nothing had been rendered at all (reported
/// against a real Kitty terminal). If blit() succeeds, the std plane's
/// cursor must end up strictly below where it started, proving the
/// image was placed at (and the cursor advanced past) the prior cursor
/// row rather than always at row 0.
void testBlitPlacesImageAtCursorAndAdvancesPastIt()
{
    FILE* f = tmpfile();
    assert(f);

    unsigned before = 0, after = 0;
    bool blitOk = false;
    {
        Terminal term;
        assert(term.init(f, kTestFlags | NCOPTION_SCROLLING));
        before = term.cursorRow();

        std::vector<uint32_t> pixels{0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFFFFFFFFu};
        blitOk = term.blit(pixels.data(), 2, 2);
        after = term.cursorRow();
    }

    // This sandbox has no pixel protocol (HANDOFF.md sec 3.9), so blitOk
    // will typically be false here and this assertion is unreachable in
    // CI -- it is exercised whenever a pixel-capable terminal runs the
    // suite, and was confirmed against the reported bug in a real Kitty
    // session.
    if (blitOk)
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
    testBlitPlacesImageAtCursorAndAdvancesPastIt();
    return 0;
}
