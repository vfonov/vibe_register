/// mriv_qc — rate PNG/JPG images Pass / Warning / Fail in the terminal.

#include <iostream>

#include "qc/QcRun.hpp"

int main(int argc, char** argv)
{
    return mriv::qc::runQc(argc, argv, std::cin, std::cout, std::cerr);
}
