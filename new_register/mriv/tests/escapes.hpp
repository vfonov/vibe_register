#pragma once

/// Escape-sequence parser for the mriv test suite (Layer A).
/// Recognizes the limited set of escape sequences our app emits:
///   - Kitty graphics: ESC _ G <key=value,...> ; <payload> ST
///   - Cursor moves:   ESC [ <row> ; <col> H
/// Any other bytes are collected as a Text event.

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace mriv::term::test
{

enum class EventKind
{
    KittyGraphics,
    CursorMove,
    Text,
};

struct EscapeEvent
{
    EventKind kind = EventKind::Text;

    // KittyGraphics: parsed key=value pairs before the ';'.
    std::map<std::string, std::string> params;

    // KittyGraphics: the base64 payload following ';' and before ST.
    std::string payload;

    // CursorMove: 1-based row/col from ESC [ <row> ; <col> H.
    std::size_t cursorRow = 0;
    std::size_t cursorCol = 0;
};

std::vector<EscapeEvent> parseEscapeStream(const std::string& bytes);

} // namespace mriv::term::test
