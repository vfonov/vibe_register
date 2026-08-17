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

} // namespace

int main()
{
    testInitLifecycle();
    testReinit();
    testBlitBeforeInitFails();
    testBlitEmptyImageFails();
    testBlitStructure();
    return 0;
}
