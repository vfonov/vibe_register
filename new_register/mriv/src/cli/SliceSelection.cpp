#include "cli/SliceSelection.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace mriv::term
{

namespace
{

bool isNumberChar(char c)
{
    return std::isdigit(static_cast<unsigned char>(c)) || c == '.';
}

} // namespace

std::optional<SliceSelection> parseSliceArg(const std::string& arg)
{
    if (arg == "mid")
        return SliceSelection{SliceSelectionKind::Mid, 0.0};

    if (arg.empty())
        return std::nullopt;

    bool isPercent = arg.back() == '%';
    std::string numPart = isPercent ? arg.substr(0, arg.size() - 1) : arg;

    if (numPart.empty())
        return std::nullopt;

    // Only accept digits and at most one decimal point; reject leading
    // '-' (negative indices are not a supported CLI spelling) and any
    // trailing garbage such as "12x".
    for (char c : numPart)
    {
        if (!isNumberChar(c))
            return std::nullopt;
    }

    char* end = nullptr;
    double value = std::strtod(numPart.c_str(), &end);
    if (end != numPart.c_str() + numPart.size())
        return std::nullopt;

    if (isPercent)
        return SliceSelection{SliceSelectionKind::Percent, value};

    return SliceSelection{SliceSelectionKind::Absolute, value};
}

namespace
{

// Split on commas, no trimming -- --slice specs have no reason to carry
// embedded spaces the way --colourmap's display names do.
std::vector<std::string> splitOnCommas(const std::string& arg)
{
    std::vector<std::string> parts;
    size_t pos = 0;
    for (;;)
    {
        size_t comma = arg.find(',', pos);
        parts.push_back(arg.substr(pos, comma == std::string::npos
                                            ? std::string::npos
                                            : comma - pos));
        if (comma == std::string::npos)
            break;
        pos = comma + 1;
    }
    return parts;
}

} // namespace

std::optional<SliceSpec> parseSliceSpec(const std::string& arg)
{
    if (arg.empty())
        return std::nullopt;

    // No "=" anywhere: the classic single-axis form, resolved by the
    // caller against --axis.
    if (arg.find('=') == std::string::npos)
    {
        auto selection = parseSliceArg(arg);
        if (!selection.has_value())
            return std::nullopt;
        SliceSpec spec;
        spec.active = selection;
        return spec;
    }

    // Otherwise every comma-separated part must be "x=...", "y=..." or
    // "z=..." -- mixing in a bare part is rejected rather than guessing
    // which axis it meant.
    SliceSpec spec;
    for (const std::string& part : splitOnCommas(arg))
    {
        size_t eq = part.find('=');
        // Exactly one character before "=": rejects a bare part mixed in
        // ("10"), a missing axis letter ("=10"), and a multi-char one.
        if (eq != 1)
            return std::nullopt;

        char axis = part[0];
        auto selection = parseSliceArg(part.substr(eq + 1));
        if (!selection.has_value())
            return std::nullopt;

        std::optional<SliceSelection>* slot = nullptr;
        switch (axis)
        {
            case 'x': slot = &spec.x; break;
            case 'y': slot = &spec.y; break;
            case 'z': slot = &spec.z; break;
            default:  return std::nullopt;
        }

        if (slot->has_value())
            return std::nullopt; // duplicate axis

        *slot = selection;
    }

    return spec;
}

int resolveSliceIndex(const SliceSelection& selection, int dimSize)
{
    int maxIndex = dimSize - 1;
    if (maxIndex < 0)
        maxIndex = 0;

    int index = 0;
    switch (selection.kind)
    {
    case SliceSelectionKind::Mid:
        index = dimSize / 2;
        break;
    case SliceSelectionKind::Absolute:
        index = static_cast<int>(selection.value);
        break;
    case SliceSelectionKind::Percent:
        index = static_cast<int>(std::lround(selection.value / 100.0 * maxIndex));
        break;
    }

    return std::clamp(index, 0, maxIndex);
}

} // namespace mriv::term
