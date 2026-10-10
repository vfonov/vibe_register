#include "interactive/StatusLine.hpp"

#include <algorithm>
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

std::vector<std::string> formatVolumeNameLines(const std::vector<std::string>& paths)
{
    std::vector<std::string> lines;
    lines.reserve(paths.size());
    for (const auto& path : paths)
        lines.push_back(baseName(path));
    return lines;
}

std::string formatSummaryLine(const ViewState& state, const std::vector<std::string>& paths)
{
    (void)paths; // kept for interface symmetry with formatStatusLine's prompt use

    std::ostringstream out;
    const VolumeDisplay& display = state.display(state.activeVolume());

    out << planeName(state.viewIndex()) << " (" << state.axis() << ")"
        // 1-based: "slice 1/96" reading as "the first of 96" is what a
        // position indicator should say, even though renderSlice() takes a
        // 0-based index.
        << " " << (state.sliceIndex() + 1) << "/" << state.sliceCount()
        << "  range " << formatValue(display.rangeLow)
        << " to " << formatValue(display.rangeHigh)
        << "  " << colourMapName(display.colourMap);

    // Only while zoomed: at 1 the row is what it always was.
    if (state.zoom() != 1.0)
        out << "  zoom " << formatValue(state.zoom()) << "x";

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

    if (state.isGoToOpen())
    {
        std::ostringstream prompt;
        prompt << "go to " << planeName(state.viewIndex()) << " slice [1-"
               << state.sliceCount() << "]: " << state.goToText() << "_";
        if (state.goToHasError())
            prompt << "   (expected 1 to " << state.sliceCount() << ")";
        return prompt.str();
    }

    return formatSummaryLine(state, paths);
}

std::string formatHotkeyLine(const ViewState& state)
{
    std::ostringstream out;
    out << "j/k slice  g goto  x/y/z axis  +/- zoom";

    // Keys that only mean something with more than one column. Offering Tab
    // when there is nothing to switch to is noise, not discoverability.
    if (state.volumeCount() > 1)
        out << "  Tab volume  o overlay";

    out << "  c map  r range  s screenshot  i info  ? help  q quit";

    return out.str();
}

std::vector<std::string> formatInfoLines(const ViewState& state,
                                         const std::vector<std::string>& paths)
{
    size_t nameWidth = 0;
    for (const auto& path : paths)
        nameWidth = std::max(nameWidth, baseName(path).size());

    std::vector<std::string> lines;
    lines.reserve(paths.size() + 2);
    for (size_t i = 0; i < paths.size(); ++i)
    {
        int volume = static_cast<int>(i);
        glm::ivec3 voxel = state.cursorVoxel(volume);

        std::ostringstream out;
        out << std::left << std::setw(static_cast<int>(nameWidth)) << baseName(paths[i])
            << "  voxel " << voxel.x << " " << voxel.y << " " << voxel.z;
        if (!state.isCursorInside(volume))
            out << "  (outside)";
        lines.push_back(out.str());
    }

    // The overlay column has no file of its own to name it.
    if (state.isOverlayVisible())
    {
        std::ostringstream overlay;
        overlay << std::left << std::setw(static_cast<int>(nameWidth)) << "overlay"
                << "  blend of " << state.volumeCount() << " volumes";
        lines.push_back(overlay.str());
    }

    glm::dvec3 world = state.cursorWorld();
    std::ostringstream out;
    out << std::fixed << std::setprecision(2)
        << "world " << world.x << " " << world.y << " " << world.z;
    lines.push_back(out.str());

    return lines;
}

std::vector<std::string> formatHelpLines(const ViewState& state)
{
    const bool columns = state.volumeCount() > 1;

    std::vector<std::string> lines;
    auto key = [&lines](const std::string& keys, const std::string& effect) {
        std::ostringstream out;
        out << "  " << std::left << std::setw(14) << keys << effect;
        lines.push_back(out.str());
    };

    lines.push_back("mriv keys");
    lines.push_back("");
    key("j / k", "move the cursor one slice along the active axis");
    key("J / K", "move ten slices");
    key("g", "go to a slice number (Enter applies, Esc cancels)");
    key("m", "centre the cursor in the active volume, on every axis");
    key("x / y / z", "choose the axis j / k moves");
    key("Up / Down", "step the active row through the displayed views");
    key("+ / -", "zoom in / out around the cursor (= and _ work too)");
    key("0", "reset the zoom");
    if (columns)
    {
        key("Tab", "next column");
        key("1-9", "select a column directly");
        key("Left / Right", "previous / next column");
        key("o", "show / hide the overlay column: all volumes blended");
    }
    key("c / C", "next / previous colour map for the active column");
    key("r", "type a new intensity range (Enter applies, Esc cancels)");
    key("s", "save the frame on screen as a PNG");
    key("i", "show / hide the loaded files and cursor positions");
    key("? / h", "this list");
    key("Ctrl-L", "redraw");
    key("q, Esc", "quit");
    lines.push_back("");
    lines.push_back("Press any key to return.");

    return lines;
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
