#include "cli/SliceSelection.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

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
