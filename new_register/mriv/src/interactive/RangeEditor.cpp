#include "interactive/RangeEditor.hpp"

#include <sstream>

namespace mriv::term
{

std::optional<std::pair<double, double>> parseRangeText(const std::string& text)
{
    // A comma is accepted because it matches --range's own syntax; a space
    // is accepted because that is what fingers do.
    std::string normalised = text;
    for (char& c : normalised)
    {
        if (c == ',')
            c = ' ';
    }

    std::istringstream in(normalised);
    double low = 0.0;
    double high = 0.0;
    if (!(in >> low >> high))
        return std::nullopt;

    // Trailing junk means the user meant something we did not understand,
    // so refuse rather than guess at the part we could read.
    std::string rest;
    if (in >> rest)
        return std::nullopt;

    if (!(low < high))
        return std::nullopt;

    return std::make_pair(low, high);
}

void RangeEditor::begin(double low, double high)
{
    text_.clear();
    error_ = false;
    value_.reset();
    currentLow_ = low;
    currentHigh_ = high;
}

EditResult RangeEditor::handleKey(char key)
{
    if (key == '\x1b')
        return EditResult::Cancelled;

    if (key == '\r' || key == '\n')
    {
        auto parsed = parseRangeText(text_);
        if (!parsed.has_value())
        {
            // Keep the prompt and the typing: the user is one correction
            // away, and clearing it would cost them the whole line.
            error_ = true;
            return EditResult::Editing;
        }

        value_ = parsed;
        error_ = false;
        return EditResult::Committed;
    }

    if (key == '\b' || key == '\x7f')
    {
        if (!text_.empty())
            text_.pop_back();
        // Backspacing an empty buffer is harmless rather than a cancel:
        // losing the prompt to one key too many would be infuriating.
        error_ = false;
        return EditResult::Editing;
    }

    if (key >= ' ' && key <= '~')
    {
        text_.push_back(key);
        error_ = false;
    }

    return EditResult::Editing;
}

} // namespace mriv::term
