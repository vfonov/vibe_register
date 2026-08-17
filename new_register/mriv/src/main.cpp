/// mriv — terminal-based slice viewer for MINC2/NIfTI volumes.

#include <iostream>

#include "cli/Run.hpp"

int main(int argc, char** argv)
{
    return mriv::term::run(argc, argv, std::cin, std::cout, std::cerr);
}
