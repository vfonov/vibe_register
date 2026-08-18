#include "interactive/StatusLine.hpp"

#include <iomanip>
#include <sstream>

namespace mriv::term
{

namespace
{

const char* planeName(int viewIndex)
{
    switch (viewIndex)
    {
        case 1:  return "sagittal";
        case 2:  return "coronal";
        default: return "axial";
    }
}

/// Four significant digits, so the line stays the same width whether the
/// range spans 0-1 (a mask) or 0-32767 (raw CT), instead of printing a raw
/// double and pushing the key legend off the row.
std::string formatValue(double value)
{
    std::ostringstream out;
    out << std::setprecision(4) << value;
    return out.str();
}

} // namespace

std::string formatStatusLine(const ViewState& state, const std::string& path)
{
    std::ostringstream out;
    out << path
        << "  " << planeName(state.viewIndex()) << " (" << state.axis() << ")"
        // 1-based: "slice 1/96" reading as "the first of 96" is what a
        // position indicator should say, even though renderSlice() takes a
        // 0-based index.
        << "  slice " << (state.sliceIndex() + 1) << "/" << state.sliceCount()
        << "  range " << formatValue(state.rangeLow())
        << " to " << formatValue(state.rangeHigh())
        << "  |  j/k slice  x/y/z axis  +/- window  q quit";
    return out.str();
}

} // namespace mriv::term
