/// test_range_editor.cpp — typing a lo/hi intensity range.
///
/// The prompt replaced the old +/- window scaling: on real data you know
/// the numbers you want, and hunting for them by repeated multiplication is
/// slower than typing them. This is a line editor with nowhere to hide, so
/// every key it accepts and every string it rejects is pinned here.

#include <cassert>

#include "interactive/RangeEditor.hpp"

using namespace mriv::term;

namespace
{

EditResult type(RangeEditor& editor, const std::string& keys)
{
    EditResult result = EditResult::Editing;
    for (char key : keys)
        result = editor.handleKey(key);
    return result;
}

void testTypingAccumulates()
{
    RangeEditor editor;
    editor.begin(0.0, 100.0);
    assert(editor.text().empty());

    type(editor, "20 180");
    assert(editor.text() == "20 180");
}

void testEnterCommitsTheTypedRange()
{
    RangeEditor editor;
    editor.begin(0.0, 100.0);

    assert(type(editor, "20 180") == EditResult::Editing);
    assert(editor.handleKey('\r') == EditResult::Committed);

    auto value = editor.value();
    assert(value.has_value());
    assert(value->first == 20.0);
    assert(value->second == 180.0);
}

/// Both separators work: a comma matches --range's own syntax, a space is
/// what fingers do.
void testCommaAndSpaceBothSeparate()
{
    RangeEditor comma;
    comma.begin(0.0, 1.0);
    type(comma, "5,9");
    assert(comma.handleKey('\n') == EditResult::Committed);
    assert(comma.value()->first == 5.0);
    assert(comma.value()->second == 9.0);

    RangeEditor spaced;
    spaced.begin(0.0, 1.0);
    type(spaced, "  5 , 9  ");
    assert(spaced.handleKey('\r') == EditResult::Committed);
    assert(spaced.value()->first == 5.0);
}

void testNegativeAndFractionalValues()
{
    RangeEditor editor;
    editor.begin(0.0, 1.0);
    type(editor, "-1.5 2.25");
    assert(editor.handleKey('\r') == EditResult::Committed);
    assert(editor.value()->first == -1.5);
    assert(editor.value()->second == 2.25);
}

void testBackspaceDeletes()
{
    RangeEditor editor;
    editor.begin(0.0, 1.0);
    type(editor, "123");
    editor.handleKey('\b');
    assert(editor.text() == "12");
    editor.handleKey('\x7f');
    assert(editor.text() == "1");

    // Backspacing an empty buffer is harmless, not a cancel: losing the
    // prompt to one key too many would be infuriating.
    editor.handleKey('\b');
    assert(editor.handleKey('\b') == EditResult::Editing);
    assert(editor.text().empty());
}

void testEscapeCancels()
{
    RangeEditor editor;
    editor.begin(0.0, 1.0);
    type(editor, "20 180");
    assert(editor.handleKey('\x1b') == EditResult::Cancelled);
}

/// A commit that cannot be parsed keeps the prompt open and says so, rather
/// than silently applying nothing or throwing the typing away.
void testBadInputKeepsThePromptOpen()
{
    RangeEditor editor;
    editor.begin(0.0, 1.0);

    type(editor, "abc");
    assert(editor.handleKey('\r') == EditResult::Editing);
    assert(editor.hasError());
    assert(editor.text() == "abc");
    assert(!editor.value().has_value());

    // Typing again clears the complaint.
    editor.handleKey('1');
    assert(!editor.hasError());
}

void testIncompleteAndTrailingJunkRejected()
{
    auto rejects = [](const std::string& typed) {
        RangeEditor editor;
        editor.begin(0.0, 1.0);
        type(editor, typed);
        bool refused = editor.handleKey('\r') == EditResult::Editing;
        return refused && editor.hasError();
    };

    assert(rejects(""));
    assert(rejects("5"));          // only one value
    assert(rejects("5 9 13"));     // one too many
    assert(rejects("5 9x"));       // trailing junk
    assert(rejects("5 abc"));
}

/// An inverted or empty range would make the colour map divide by zero
/// width, so it is refused at the prompt rather than passed on.
void testLowMustBeBelowHigh()
{
    auto rejects = [](const std::string& typed) {
        RangeEditor editor;
        editor.begin(0.0, 1.0);
        type(editor, typed);
        return editor.handleKey('\r') == EditResult::Editing && editor.hasError();
    };

    assert(rejects("9 5"));
    assert(rejects("5 5"));
}

/// begin() remembers the range in force, so the prompt can show what it is
/// replacing, and clears anything left from a previous prompt.
void testBeginRemembersTheCurrentRangeAndClearsTheBuffer()
{
    RangeEditor editor;
    editor.begin(0.0, 1.0);
    type(editor, "junk");
    editor.handleKey('\r');
    assert(editor.hasError());

    editor.begin(-5.0, 42.0);
    assert(editor.text().empty());
    assert(!editor.hasError());
    assert(editor.currentLow() == -5.0);
    assert(editor.currentHigh() == 42.0);
}

} // namespace

int main()
{
    testTypingAccumulates();
    testEnterCommitsTheTypedRange();
    testCommaAndSpaceBothSeparate();
    testNegativeAndFractionalValues();
    testBackspaceDeletes();
    testEscapeCancels();
    testBadInputKeepsThePromptOpen();
    testIncompleteAndTrailingJunkRejected();
    testLowMustBeBelowHigh();
    testBeginRemembersTheCurrentRangeAndClearsTheBuffer();
    return 0;
}
