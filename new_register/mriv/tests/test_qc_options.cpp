// mriv_qc command line.

#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "qc/QcOptions.hpp"

using namespace mriv::qc;
using Catch::Matchers::ContainsSubstring;

namespace
{

QcParseResult parse(std::vector<std::string> args, std::ostream& err)
{
    args.insert(args.begin(), "mriv_qc");
    std::vector<char*> argv;
    for (auto& a : args)
        argv.push_back(a.data());
    return parseQcArgs(static_cast<int>(argv.size()), argv.data(), err);
}

} // namespace

TEST_CASE("positional arguments are image files", "[qc][options]")
{
    std::ostringstream err;
    auto r = parse({"a.png", "b.jpg"}, err);
    REQUIRE(r.ok);
    CHECK(r.options.images == std::vector<std::string>{"a.png", "b.jpg"});
    CHECK(r.options.inputCsv.empty());
    CHECK_FALSE(r.options.readStdin);
    CHECK(r.options.output == "qc_results.csv");
    CHECK(r.options.autoAdvance);
    CHECK(r.options.scale == 0);
}

TEST_CASE("a lone '-' reads the image list from stdin", "[qc][options]")
{
    std::ostringstream err;
    auto r = parse({"-", "-o", "out.csv"}, err);
    REQUIRE(r.ok);
    CHECK(r.options.readStdin);
    CHECK(r.options.images.empty());
    CHECK(r.options.output == "out.csv");
}

TEST_CASE("-i takes an id,visit,picture CSV", "[qc][options]")
{
    std::ostringstream err;
    auto r = parse({"-i", "in.csv", "--output", "res.csv", "--no-advance", "--scale", "2"}, err);
    REQUIRE(r.ok);
    CHECK(r.options.inputCsv == "in.csv");
    CHECK(r.options.output == "res.csv");
    CHECK_FALSE(r.options.autoAdvance);
    CHECK(r.options.scale == 2);
}

TEST_CASE("exactly one input source is required", "[qc][options]")
{
    std::ostringstream err;
    CHECK_FALSE(parse({}, err).ok);
    CHECK_THAT(err.str(), ContainsSubstring("no images"));

    std::ostringstream err2;
    CHECK_FALSE(parse({"-i", "in.csv", "a.png"}, err2).ok);
    CHECK_THAT(err2.str(), ContainsSubstring("only one"));

    std::ostringstream err3;
    CHECK_FALSE(parse({"-", "a.png"}, err3).ok);
    CHECK_THAT(err3.str(), ContainsSubstring("only one"));
}

TEST_CASE("--scale must be positive", "[qc][options]")
{
    std::ostringstream err;
    CHECK_FALSE(parse({"--scale", "0", "a.png"}, err).ok);
    CHECK_THAT(err.str(), ContainsSubstring("--scale"));
}

TEST_CASE("unknown options are rejected", "[qc][options]")
{
    std::ostringstream err;
    CHECK_FALSE(parse({"--bogus", "a.png"}, err).ok);
    CHECK_FALSE(err.str().empty());
}

TEST_CASE("--help and --version need no input", "[qc][options]")
{
    std::ostringstream err;
    auto h = parse({"--help"}, err);
    REQUIRE(h.ok);
    CHECK(h.options.help);
    auto v = parse({"--version"}, err);
    REQUIRE(v.ok);
    CHECK(v.options.version);

    std::ostringstream out;
    printQcHelp(out);
    CHECK_THAT(out.str(), ContainsSubstring("mriv_qc"));
    CHECK_THAT(out.str(), ContainsSubstring("--output"));
}
