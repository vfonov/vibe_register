/// test_structure.cpp — Layer A escape-sequence structure tests.
///
/// These tests bypass the real terminal and assert on the *shape* of the
/// bytes our encoder (and the test-mode Terminal) produces.

#include <cassert>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "escapes.hpp"
#include "render/Encode.hpp"
#include "render/PixelProtocol.hpp"
#include "render/Terminal.hpp"

using namespace mriv::term;
using namespace mriv::term::test;

namespace
{

// Build a non-const argv from string literals.
std::vector<char*> makeArgv(const std::vector<std::string>& args)
{
    std::vector<char*> argv;
    for (const auto& a : args)
        argv.push_back(const_cast<char*>(a.c_str()));
    return argv;
}

std::vector<std::uint8_t> makeGradientRgba(int w, int h)
{
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            out[i + 0] = static_cast<std::uint8_t>(x * 255 / (w - 1));
            out[i + 1] = static_cast<std::uint8_t>(y * 255 / (h - 1));
            out[i + 2] = 0;
            out[i + 3] = 255;
        }
    }
    return out;
}

void testParserRecognizesKittyEvent()
{
    std::string bytes = "\x1b_Ga=T,f=100,s=8,v=8;payload-gibberish\x1b\\";
    auto events = parseEscapeStream(bytes);
    assert(events.size() == 1);
    assert(events[0].kind == EventKind::KittyGraphics);
    assert(events[0].params["a"] == "T");
    assert(events[0].params["f"] == "100");
    assert(events[0].params["s"] == "8");
    assert(events[0].params["v"] == "8");
    assert(events[0].payload == "payload-gibberish");
}

void testEncoderProducesParseableKittyEvent()
{
    auto rgba = makeGradientRgba(4, 4);
    std::string bytes = encodeKittyPng(rgba.data(), 4, 4);
    assert(!bytes.empty());

    auto events = parseEscapeStream(bytes);
    assert(events.size() == 1);
    assert(events[0].kind == EventKind::KittyGraphics);
    assert(events[0].params["a"] == "T");
    assert(events[0].params["f"] == "100");
    assert(events[0].params["s"] == "4");
    assert(events[0].params["v"] == "4");
    assert(!events[0].payload.empty());
}

void testTerminalTestModeBlitsKittyBytes()
{
    std::ostringstream out;
    {
        Terminal term(out, PixelProtocol::Kitty);
        assert(term.hasPixelSupport());

        auto rgba = makeGradientRgba(2, 2);
        bool ok = term.blit(reinterpret_cast<const uint32_t*>(rgba.data()), 2, 2);
        assert(ok);
    }

    auto events = parseEscapeStream(out.str());
    assert(events.size() == 1);
    assert(events[0].kind == EventKind::KittyGraphics);
    assert(events[0].params["s"] == "2");
    assert(events[0].params["v"] == "2");
}

void testTerminalTestModeNoneFails()
{
    std::ostringstream out;
    Terminal term(out, PixelProtocol::None);
    assert(!term.hasPixelSupport());

    auto rgba = makeGradientRgba(2, 2);
    uint32_t pixel = 0xFFFFFFFF;
    bool ok = term.blit(&pixel, 1, 1);
    assert(!ok);
    assert(out.str().empty());
}

} // namespace

int main()
{
    testParserRecognizesKittyEvent();
    testEncoderProducesParseableKittyEvent();
    testTerminalTestModeBlitsKittyBytes();
    testTerminalTestModeNoneFails();
    return 0;
}
