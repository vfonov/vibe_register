#include "Prefetcher.h"

#include <iostream>

#include "AppState.h"  // VolumeCache, Volume
#include "Volume.h"

Prefetcher::Prefetcher(VolumeCache& cache)
    : cache_(cache)
{
}

void Prefetcher::requestPrefetch(const std::vector<std::string>& paths)
{
    pendingPaths_ = paths;
}

void Prefetcher::cancelPending()
{
    pendingPaths_.clear();
}

bool Prefetcher::loadPending()
{
    while (!pendingPaths_.empty())
    {
        std::string path = std::move(pendingPaths_.back());
        pendingPaths_.pop_back();

        // Skip empty paths.
        if (path.empty())
            continue;

        // Skip if already cached.
        if (cache_.get(path) != nullptr)
            continue;

        // Load the volume from disk (synchronously on the main thread).
        try
        {
            Volume vol;
            vol.load(path);
            cache_.put(path, vol);
            if (debugLoggingEnabled())
                std::cerr << "[prefetch] cached: " << path << "\n";
        }
        catch (const std::exception& e)
        {
            if (debugLoggingEnabled())
                std::cerr << "[prefetch] failed: " << path
                          << " (" << e.what() << ")\n";
        }

        // Loaded (or failed) one volume — return to avoid stalling the UI.
        return true;
    }

    return false;
}
