#include "cli/ColourMapArg.hpp"

#include <algorithm>
#include <cctype>

namespace mriv::term
{

namespace
{

std::string normalise(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        if (c == ' ' || c == '_' || c == '-')
            continue;
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

} // namespace

std::optional<ColourMapType> resolveColourMapArg(const std::string& arg)
{
    if (arg.empty())
        return std::nullopt;

    std::string target = normalise(arg);
    for (int i = 0; i < colourMapCount(); ++i)
    {
        auto type = static_cast<ColourMapType>(i);
        if (normalise(colourMapName(type)) == target)
            return type;
    }
    return std::nullopt;
}

std::string listColourMapNames()
{
    std::string out;
    for (int i = 0; i < colourMapCount(); ++i)
    {
        if (i > 0)
            out += ", ";
        auto name = colourMapName(static_cast<ColourMapType>(i));
        out.append(name.data(), name.size());
    }
    return out;
}

} // namespace mriv::term
