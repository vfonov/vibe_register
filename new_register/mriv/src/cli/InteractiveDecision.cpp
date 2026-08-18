#include "cli/InteractiveDecision.hpp"

namespace mriv::term
{

InteractiveDecision decideInteractive(const Options& options, bool stdoutIsTty)
{
    InteractiveDecision decision;

    if (options.interactive && options.noInteractive)
    {
        decision.refusal = "--interactive and --no-interactive cannot be combined";
        return decision;
    }

    if (options.interactive)
    {
        if (options.info)
            decision.refusal = "--interactive cannot be combined with --info";
        else if (options.files.size() != 1)
            decision.refusal = "--interactive needs exactly one file to navigate";
        else if (!stdoutIsTty)
            decision.refusal = "--interactive needs a terminal on stdout";
        else
            decision.interactive = true;

        return decision;
    }

    if (options.noInteractive || options.info)
        return decision;

    decision.interactive = stdoutIsTty && options.files.size() == 1;
    return decision;
}

} // namespace mriv::term
