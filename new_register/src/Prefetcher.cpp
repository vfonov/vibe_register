#include "Prefetcher.h"

#include <iostream>

#include "AppState.h"  // VolumeCache, Volume
#include "Volume.h"

Prefetcher::Prefetcher(VolumeCache& cache)
    : cache_(cache)
{
    thread_ = std::thread(&Prefetcher::workerLoop, this);
}

Prefetcher::~Prefetcher()
{
    shutdown();
}

void Prefetcher::requestPrefetch(const std::vector<std::string>& paths)
{
    {
        std::lock_guard<std::mutex> lk(mutex_);
        pendingPaths_ = paths;
        hasWork_ = true;
    }
    // Cancel any currently running load so the worker picks up the new
    // request quickly.
    cancelFlag_.store(true, std::memory_order_relaxed);
    cv_.notify_one();
}

void Prefetcher::cancelPending()
{
    {
        std::lock_guard<std::mutex> lk(mutex_);
        pendingPaths_.clear();
        hasWork_ = false;
    }
    cancelFlag_.store(true, std::memory_order_relaxed);
}

void Prefetcher::shutdown()
{
    exitFlag_.store(true, std::memory_order_relaxed);
    cancelFlag_.store(true, std::memory_order_relaxed);
    cv_.notify_one();
    if (thread_.joinable())
        thread_.join();
}

void Prefetcher::workerLoop()
{
    while (true)
    {
        // Wait for work or exit signal.
        std::vector<std::string> paths;
        {
            std::unique_lock<std::mutex> lk(mutex_);
            cv_.wait(lk, [this] { return hasWork_ || exitFlag_.load(std::memory_order_relaxed); });

            if (exitFlag_.load(std::memory_order_relaxed))
                return;

            paths = std::move(pendingPaths_);
            pendingPaths_.clear();
            hasWork_ = false;
        }

        // Reset the cancel flag now that we have a fresh batch.
        cancelFlag_.store(false, std::memory_order_relaxed);

        for (const auto& path : paths)
        {
            // Check for cancellation between each volume load.
            if (cancelFlag_.load(std::memory_order_relaxed))
                break;
            if (exitFlag_.load(std::memory_order_relaxed))
                return;

            // Skip empty paths.
            if (path.empty())
                continue;

            // Skip if already cached.
            if (cache_.get(path) != nullptr)
                continue;

            // Load the volume from disk.
            try
            {
                Volume vol;
                vol.load(path);
                cache_.put(path, vol);
                std::cerr << "[prefetch] cached: " << path << "\n";
            }
            catch (const std::exception& e)
            {
                std::cerr << "[prefetch] failed: " << path
                          << " (" << e.what() << ")\n";
            }
        }
    }
}
