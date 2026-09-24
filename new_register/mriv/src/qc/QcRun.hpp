#pragma once

#include <iosfwd>

namespace mriv::qc
{

/// mriv_qc's whole program: parse, load the image list, run the session on
/// the terminal, save and print a summary. Returns the process exit status.
///
/// Terminal glue only -- every decision it makes is delegated to the tested
/// pieces (QcOptions, QcState, QcStatusLine, QcImage). Keep it that way.
int runQc(int argc, char** argv, std::istream& in, std::ostream& out, std::ostream& err);

} // namespace mriv::qc
