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
        else if (!stdoutIsTty)
            decision.refusal = "--interactive needs a terminal on stdout";
        else
            decision.interactive = true;

        return decision;
    }

    if (options.noInteractive || options.info)
        return decision;

    // The file count no longer decides this: several volumes become
    // columns of one navigable grid, which is more use interactively than
    // piped. A terminal on stdout is the only requirement.
    decision.interactive = stdoutIsTty;
    return decision;
}

} // namespace mriv::term
