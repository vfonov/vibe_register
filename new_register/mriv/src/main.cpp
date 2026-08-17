/// mriv — terminal-based slice viewer for MINC2/NIfTI volumes.

#include <iostream>

#include "cli/Options.hpp"

int main(int argc, char** argv)
{
    auto parsed = mriv::term::parseArgs(argc, argv);
    if (!parsed.ok)
        return 1;

    if (parsed.options.help)
    {
        mriv::term::printHelp();
        return 0;
    }

    if (parsed.options.files.empty())
    {
        std::cerr << "mriv: no input files given\n";
        mriv::term::printHelp();
        return 1;
    }

    std::cerr << "mriv: rendering not yet implemented\n";
    return 1;
}
