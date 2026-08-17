/// test_colourmap_arg.cpp — normalised --colourmap name resolution.
/// See mriv/HANDOFF.md sec 3.4: colourMapName() does an exact match against
/// display names like "Hot Metal"; the CLI must normalise both sides so
/// "hotmetal" / "hot-metal" / "hot_metal" / "Hot Metal" all resolve.

#include <cassert>

#include "cli/ColourMapArg.hpp"

using namespace mriv::term;

namespace
{

void testExactNameMatches()
{
    assert(resolveColourMapArg("Gray").value() == ColourMapType::GrayScale);
    assert(resolveColourMapArg("Hot Metal").value() == ColourMapType::HotMetal);
    assert(resolveColourMapArg("Viridis").value() == ColourMapType::Viridis);
}

void testCaseInsensitive()
{
    assert(resolveColourMapArg("gray").value() == ColourMapType::GrayScale);
    assert(resolveColourMapArg("GRAY").value() == ColourMapType::GrayScale);
    assert(resolveColourMapArg("viridis").value() == ColourMapType::Viridis);
}

void testSeparatorsIgnored()
{
    assert(resolveColourMapArg("hotmetal").value() == ColourMapType::HotMetal);
    assert(resolveColourMapArg("hot-metal").value() == ColourMapType::HotMetal);
    assert(resolveColourMapArg("hot_metal").value() == ColourMapType::HotMetal);
    assert(resolveColourMapArg("Hot Metal").value() == ColourMapType::HotMetal);
    assert(resolveColourMapArg("Cold-Metal").value() == ColourMapType::ColdMetal);
}

void testUnknownNameRejected()
{
    assert(!resolveColourMapArg("not-a-colourmap").has_value());
    assert(!resolveColourMapArg("").has_value());
}

void testListIncludesAllNames()
{
    std::string list = listColourMapNames();
    assert(list.find("Gray") != std::string::npos);
    assert(list.find("Hot Metal") != std::string::npos);
    assert(list.find("Turbo") != std::string::npos);
}

} // namespace

int main()
{
    testExactNameMatches();
    testCaseInsensitive();
    testSeparatorsIgnored();
    testUnknownNameRejected();
    testListIncludesAllNames();
    return 0;
}
