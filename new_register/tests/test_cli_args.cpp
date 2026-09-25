/// test_cli_args.cpp — parseArgs() command-line parsing.
///
/// Covers: malformed numbers for --range and --scale are reported as a
/// parse error (nullopt) instead of escaping as std::invalid_argument and
/// aborting the program; and a label description file (-L) marks the next
/// volume as a label volume, since its LUT is only used for label volumes.

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "CliArgs.h"

namespace
{

std::optional<ParsedArgs> parse(std::vector<std::string> words)
{
    words.insert(words.begin(), "new_register");
    std::vector<char*> argv;
    for (auto& w : words)
        argv.push_back(w.data());
    argv.push_back(nullptr);
    return parseArgs(static_cast<int>(words.size()), argv.data());
}

}  // namespace

TEST_CASE("A valid --range and --scale are parsed", "[cli]")
{
    auto args = parse({"--scale", "1.5", "--range", "-10.5,1e3", "a.mnc"});
    REQUIRE(args);
    REQUIRE(args->scaleFactor);
    CHECK(*args->scaleFactor == 1.5f);
    REQUIRE(args->perVolOpts.size() == 1);
    REQUIRE(args->perVolOpts[0].range);
    CHECK((*args->perVolOpts[0].range)[0] == -10.5);
    CHECK((*args->perVolOpts[0].range)[1] == 1000.0);
}

TEST_CASE("A malformed --range is a parse error", "[cli]")
{
    const std::string range = GENERATE("abc,1", "0,xyz", ",", "1,2x", "1e999,2", "nan,1");
    INFO("--range " << range);
    std::optional<ParsedArgs> args;
    REQUIRE_NOTHROW(args = parse({"--range", range, "a.mnc"}));
    CHECK_FALSE(args);
}

TEST_CASE("A malformed --scale is a parse error", "[cli]")
{
    const std::string scale = GENERATE("x", "", "2x", "0", "-1", "inf");
    INFO("--scale " << scale);
    std::optional<ParsedArgs> args;
    REQUIRE_NOTHROW(args = parse({"--scale", scale, "a.mnc"}));
    CHECK_FALSE(args);
}

TEST_CASE("-L marks the next volume as a label volume", "[cli][labels]")
{
    auto args = parse({"-L", "labels.txt", "seg.mnc", "t1.mnc"});
    REQUIRE(args);
    REQUIRE(args->perVolOpts.size() == 2);
    CHECK(args->perVolOpts[0].isLabel);
    CHECK(args->perVolOpts[0].labelDescFile == std::optional<std::string>("labels.txt"));
    CHECK_FALSE(args->perVolOpts[1].isLabel);
}
