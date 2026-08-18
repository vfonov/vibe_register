#include "cli/ViewList.hpp"

#include <algorithm>

#include "render/SliceGeometry.hpp"

namespace mriv::term
{

namespace
{

std::string trim(const std::string& s)
{
    size_t begin = s.find_first_not_of(" \t");
    if (begin == std::string::npos)
        return std::string();
    size_t end = s.find_last_not_of(" \t");
    return s.substr(begin, end - begin + 1);
}

} // namespace

std::optional<std::vector<int>> parseViewList(const std::string& arg)
{
    std::vector<int> views;

    size_t pos = 0;
    for (;;)
    {
        size_t comma = arg.find(',', pos);
        std::string element = trim(arg.substr(pos, comma == std::string::npos
                                                      ? std::string::npos
                                                      : comma - pos));

        // An empty element means the user wrote "", "z,," or ",": there is
        // no sensible view to infer, so the whole list is rejected rather
        // than silently rendering fewer rows than asked for.
        if (element.size() != 1)
            return std::nullopt;

        auto viewIndex = viewIndexForAxis(element[0]);
        if (!viewIndex.has_value())
            return std::nullopt;

        if (std::find(views.begin(), views.end(), *viewIndex) == views.end())
            views.push_back(*viewIndex);

        if (comma == std::string::npos)
            break;
        pos = comma + 1;
    }

    return views;
}

} // namespace mriv::term
