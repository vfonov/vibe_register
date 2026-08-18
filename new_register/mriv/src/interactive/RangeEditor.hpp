#pragma once

#include <optional>
#include <string>
#include <utility>

namespace mriv::term
{

/// What a keypress did to the prompt.
enum class EditResult
{
    /// Still typing -- including a commit that could not be parsed, which
    /// keeps the prompt open with a complaint rather than throwing the
    /// typing away.
    Editing,
    /// Enter, with a valid range in value().
    Committed,
    /// Esc; the range in force is unchanged.
    Cancelled,
};

/// A one-line editor for typing an intensity range.
///
/// This replaced the old +/- window scaling: on real data the numbers you
/// want are known, and hunting for them by repeated multiplication is
/// slower than typing them. Pure -- no terminal, no volume -- so all of it
/// is testable on a host with no TTY (mriv/HANDOFF.md sec 3.9).
class RangeEditor
{
public:
    /// Open a prompt, remembering the range currently in force so it can be
    /// shown as what is being replaced. Clears any previous buffer.
    void begin(double low, double high);

    /// Apply one keypress: printable characters append, Backspace or DEL
    /// deletes, Enter commits, Esc cancels. Anything else is ignored.
    EditResult handleKey(char key);

    const std::string& text() const { return text_; }

    /// True when the last commit could not be parsed. Cleared by any edit.
    bool hasError() const { return error_; }

    double currentLow() const { return currentLow_; }
    double currentHigh() const { return currentHigh_; }

    /// The committed range. Empty unless the last handleKey() returned
    /// Committed.
    std::optional<std::pair<double, double>> value() const { return value_; }

private:
    std::string text_;
    bool error_ = false;
    double currentLow_ = 0.0;
    double currentHigh_ = 0.0;
    std::optional<std::pair<double, double>> value_;
};

/// Parse a typed range: "low high" or "low,high". Returns std::nullopt for
/// anything else, including a single value, trailing junk, and a low that
/// is not below its high -- an inverted or empty range would leave the
/// colour map with no width to map across.
std::optional<std::pair<double, double>> parseRangeText(const std::string& text);

} // namespace mriv::term
