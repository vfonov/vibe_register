#pragma once

#include <atomic>

/// Global flag toggled by `--debug` / `-d`. Used to gate verbose stderr
/// output from library code (Volume, ViewManager, AppState, etc.) without
/// pulling in the full AppState.h dependency chain.
inline std::atomic<bool>& debugLoggingEnabled()
{
    static std::atomic<bool> flag{false};
    return flag;
}
