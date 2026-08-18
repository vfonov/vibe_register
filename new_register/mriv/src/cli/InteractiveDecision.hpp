#pragma once

#include <string>

#include "cli/Options.hpp"

namespace mriv::term
{

/// Whether this invocation should enter interactive mode, and if it was
/// explicitly asked for but cannot be, why not.
struct InteractiveDecision
{
    bool interactive = false;

    /// Non-empty only when --interactive was requested and cannot be
    /// honoured. An explicit request is refused with a reason rather than
    /// silently downgraded to a one-shot render: the user asked for a
    /// navigable view, and quietly printing a single slice instead looks
    /// like the keys are broken.
    std::string refusal;
};

/// Decide whether to run interactively. Interactive mode needs a terminal
/// to read keys from, and that is the only requirement: several volumes
/// become columns of one navigable grid rather than a reason to refuse.
/// --interactive forces the question to be asked out loud; --no-interactive
/// is the escape hatch for a user at a TTY who wants the one-shot "cat for
/// medical images" behaviour anyway.
InteractiveDecision decideInteractive(const Options& options, bool stdoutIsTty);

} // namespace mriv::term
