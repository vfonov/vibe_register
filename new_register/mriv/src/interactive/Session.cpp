#include "interactive/Session.hpp"

namespace mriv::term
{

int runSession(ViewState& state, const KeySource& nextKey, const FrameSink& draw)
{
    if (!draw(state))
        return 1;

    for (;;)
    {
        std::optional<char> key = nextKey();
        if (!key.has_value())
            return 0;

        KeyResult result = state.handleKey(*key);
        if (result == KeyResult::Quit)
            return 0;
        if (result == KeyResult::Ignored)
            continue;

        if (!draw(state))
            return 1;
    }
}

} // namespace mriv::term
