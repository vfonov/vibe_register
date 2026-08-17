#pragma once

#include <iosfwd>

namespace mriv::term
{

/// Main entry point for the mriv CLI, separated from `src/main.cpp` so
/// integration tests can call it with injected streams. Returns the
/// exit code the process should use.
int run(int argc, char** argv, std::istream& in, std::ostream& out, std::ostream& err);

} // namespace mriv::term
