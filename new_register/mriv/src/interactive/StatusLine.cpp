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

/// The file name only: a full path would take the whole row on its own.
std::string baseName(const std::string& path)
{
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string nameFor(const std::vector<std::string>& paths, int index)
{
    if (index < 0 || static_cast<size_t>(index) >= paths.size())
        return std::string("?");
    return baseName(paths[static_cast<size_t>(index)]);
}

} // namespace

std::string formatSummaryLine(const ViewState& state, const std::vector<std::string>& paths)
{
    std::ostringstream out;

    // Every column, in order, with the active one starred: 'c' and the
    // range act on it, so which one that is has to be visible.
    for (int i = 0; i < state.volumeCount(); ++i)
    {
        if (i > 0)
            out << " ";
        out << nameFor(paths, i);
        if (i == state.activeVolume())
            out << "*";
    }

    const VolumeDisplay& display = state.display(state.activeVolume());

    out << "  |  " << planeName(state.viewIndex()) << " (" << state.axis() << ")"
        // 1-based: "slice 1/96" reading as "the first of 96" is what a
        // position indicator should say, even though renderSlice() takes a
        // 0-based index.
        << " " << (state.sliceIndex() + 1) << "/" << state.sliceCount()
        << "  range " << formatValue(display.rangeLow)
        << " to " << formatValue(display.rangeHigh)
        << "  " << colourMapName(display.colourMap);

    return out.str();
}

std::string formatStatusLine(const ViewState& state, const std::vector<std::string>& paths)
{
    // One reserved row means the prompt and the summary cannot both be on
    // screen, and while the user is typing the prompt is what matters.
    if (state.isEditing())
    {
        const RangeEditor& editor = state.editor();
        std::ostringstream prompt;
        prompt << "range for " << nameFor(paths, state.activeVolume())
               << " [" << formatValue(editor.currentLow())
               << " " << formatValue(editor.currentHigh()) << "]: "
               << editor.text() << "_";
        if (editor.hasError())
            prompt << "   (expected \"low high\", low below high)";
        return prompt.str();
    }

    // The live row is the summary plus the legend, so the two can never
    // disagree about what is on screen.
    std::ostringstream out;
    out << formatSummaryLine(state, paths) << "  |  j/k slice  x/y/z axis";

    // Keys that only mean something with more than one column. Offering Tab
    // when there is nothing to switch to is noise, not discoverability.
    if (state.volumeCount() > 1)
        out << "  Tab volume";

    out << "  c map  r range  q quit";

    return out.str();
}

std::string formatCaption(const std::vector<std::string>& paths)
{
    std::ostringstream out;
    for (size_t i = 0; i < paths.size(); ++i)
    {
        if (i > 0)
            out << "   ";
        out << (i + 1) << ": " << baseName(paths[i]);
    }
    return out.str();
}

} // namespace mriv::term
