#pragma once

/// nr_test.h — Shared helpers for Catch2 tests linked with nr_test_main.
///
/// Fixture locations come from the command line (set by CMake in add_test()),
/// never from paths compiled into the source:
///   --data-dir <dir>   repository test_data/ directory
///   --src-dir  <dir>   new_register/tests/ (sq1.mnc, reference PNGs, tag files)
/// Both are empty when the option was not given. A test that needs one must
/// REQUIRE it to be non-empty — a missing fixture fails, it never skips.

#include <string>

namespace nrtest
{

/// Directory given with --data-dir, or an empty string.
const std::string& dataDir();

/// Directory given with --src-dir, or an empty string.
const std::string& srcDir();

} // namespace nrtest
