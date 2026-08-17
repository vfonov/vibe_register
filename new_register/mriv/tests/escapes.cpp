#include "escapes.hpp"

#include <cassert>
#include <cctype>
#include <cstdlib>

namespace mriv::term::test
{

namespace
{

void flushText(std::vector<EscapeEvent>& out, std::string& textBuf)
{
    if (textBuf.empty())
        return;
    EscapeEvent e;
    e.kind = EventKind::Text;
    e.payload = std::move(textBuf);
    textBuf.clear();
    out.push_back(std::move(e));
}

std::map<std::string, std::string> parseKittyParams(const std::string& keyvals)
{
    std::map<std::string, std::string> params;
    std::size_t start = 0;
    while (start <= keyvals.size())
    {
        std::size_t comma = keyvals.find(',', start);
        std::string kv    = keyvals.substr(start, comma - start);
        std::size_t eq    = kv.find('=');
        if (eq != std::string::npos)
        {
            params[kv.substr(0, eq)] = kv.substr(eq + 1);
        }
        else if (!kv.empty())
        {
            params[kv] = "";
        }
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return params;
}

} // namespace

std::vector<EscapeEvent> parseEscapeStream(const std::string& bytes)
{
    std::vector<EscapeEvent> events;
    std::string textBuf;

    for (std::size_t i = 0; i < bytes.size();)
    {
        if (bytes[i] != '\x1b')
        {
            textBuf.push_back(bytes[i]);
            ++i;
            continue;
        }

        if (i + 1 >= bytes.size())
        {
            textBuf.push_back(bytes[i]);
            ++i;
            continue;
        }

        char c2 = bytes[i + 1];
        if (c2 == '_') // Kitty graphics introducer
        {
            if (i + 3 >= bytes.size() || bytes[i + 2] != 'G')
            {
                textBuf.push_back(bytes[i]);
                ++i;
                continue;
            }

            flushText(events, textBuf);
            EscapeEvent event;
            event.kind = EventKind::KittyGraphics;

            std::size_t headerStart = i + 3; // after ESC _ G
            std::size_t semi        = bytes.find(';', headerStart);
            if (semi == std::string::npos)
            {
                // Malformed; treat rest as text.
                textBuf.append(bytes.substr(i));
                break;
            }

            event.params = parseKittyParams(bytes.substr(headerStart, semi - headerStart));

            // Find terminator: ESC backslash.
            std::size_t term = bytes.find("\x1b\\", semi + 1);
            if (term == std::string::npos)
            {
                // Unterminated; record what we have and stop parsing.
                event.payload = bytes.substr(semi + 1);
                events.push_back(std::move(event));
                break;
            }

            event.payload = bytes.substr(semi + 1, term - (semi + 1));
            events.push_back(std::move(event));
            i = term + 2;
        }
        else if (c2 == '[') // Cursor move
        {
            std::size_t seqEnd = i + 2;
            while (seqEnd < bytes.size() && bytes[seqEnd] != 'H')
                ++seqEnd;

            if (seqEnd >= bytes.size())
            {
                // No terminator; treat as text.
                textBuf.push_back(bytes[i]);
                ++i;
                continue;
            }

            flushText(events, textBuf);
            EscapeEvent event;
            event.kind = EventKind::CursorMove;

            std::string inner(bytes, i + 2, seqEnd - (i + 2));
            std::size_t semi = inner.find(';');
            if (semi != std::string::npos)
            {
                event.cursorRow = static_cast<std::size_t>(
                    std::max(0, std::atoi(inner.substr(0, semi).c_str())));
                event.cursorCol = static_cast<std::size_t>(
                    std::max(0, std::atoi(inner.substr(semi + 1).c_str())));
            }
            events.push_back(std::move(event));
            i = seqEnd + 1;
        }
        else
        {
            textBuf.push_back(bytes[i]);
            ++i;
        }
    }

    flushText(events, textBuf);
    return events;
}

} // namespace mriv::term::test
