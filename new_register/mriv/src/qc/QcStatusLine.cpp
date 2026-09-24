#include "qc/QcStatusLine.hpp"

namespace mriv::qc
{

namespace
{

/// CSV fields may carry quoted newlines; every control character becomes a
/// space so a row stays one row.
std::string oneLine(std::string text)
{
    for (char& c : text)
    {
        if (static_cast<unsigned char>(c) < 0x20 || c == '\x7f')
            c = ' ';
    }
    return text;
}

std::string tallies(const QcState& state)
{
    QcState::Counts c = state.counts();
    size_t rated = c.pass + c.warning + c.fail;
    return "rated " + std::to_string(rated) + "/" + std::to_string(state.size())
           + " (P" + std::to_string(c.pass) + " W" + std::to_string(c.warning)
           + " F" + std::to_string(c.fail) + ")";
}

} // namespace

std::string formatHeader(const QcState& state)
{
    if (state.empty())
        return "no images";

    const QC::QCRecord& record = state.current();
    std::string header = std::to_string(state.index() + 1) + "/" + std::to_string(state.size())
                         + "  " + record.id;
    if (!record.visit.empty() && record.visit != "-")
        header += "  " + record.visit;
    header += "  [" + (record.qc_status.empty() ? std::string("--") : record.qc_status) + "]";
    header += "  " + tallies(state);
    return oneLine(header);
}

std::string formatDetailLine(const QcState& state, bool loadFailed)
{
    if (!state.message().empty())
        return oneLine(state.message());
    if (state.empty())
        return {};

    const std::string& path = state.current().picture_path;
    return oneLine(loadFailed ? "cannot load " + path : path);
}

std::string formatHotkeyLine()
{
    return "p pass  w warn  f fail  u clear  arrows prev/next  "
           "[/] unrated  g/G first/last  s save  S shot  q quit";
}

std::string formatSummary(const QcState& state, const std::string& outputPath)
{
    return tallies(state) + " -> " + oneLine(outputPath);
}

} // namespace mriv::qc
