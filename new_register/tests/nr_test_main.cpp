/// nr_test_main.cpp — main() shared by all Catch2 tests (the nr_test_main library).
///
/// Adds --data-dir and --src-dir to Catch2's own command line; see nr_test.h.

#include <string>

#include <catch2/catch_session.hpp>

#include "nr_test.h"

namespace
{

std::string gDataDir;
std::string gSrcDir;

} // namespace

namespace nrtest
{

const std::string& dataDir()
{
    return gDataDir;
}

const std::string& srcDir()
{
    return gSrcDir;
}

} // namespace nrtest

int main(int argc, char* argv[])
{
    Catch::Session session;

    using Catch::Clara::Opt;
    auto cli = session.cli()
        | Opt(gDataDir, "dir")["--data-dir"]("test_data/ directory with the sample volumes")
        | Opt(gSrcDir, "dir")["--src-dir"]("new_register/tests/ directory with fixtures and reference images");
    session.cli(cli);

    int rc = session.applyCommandLine(argc, argv);
    if (rc != 0)
    {
        return rc;
    }
    return session.run();
}
