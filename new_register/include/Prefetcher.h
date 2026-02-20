#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class VolumeCache;

/// Background prefetcher for QC mode.  Loads volumes for adjacent QC rows
/// into the shared VolumeCache on a background thread so that row switches
/// are instant (cache hit) rather than blocking on disk I/O.
///
/// Usage:
///   1. Construct with a reference to the shared VolumeCache.
///   2. After each row switch, call requestPrefetch() with the paths for
///      the neighbouring rows (prev + next).
///   3. Call shutdown() before destruction (or let the destructor do it).
///
/// Thread safety: all public methods are safe to call from the main thread.
/// The background thread only calls VolumeCache::get() and ::put(), which
/// are themselves mutex-protected.
class Prefetcher {
public:
    explicit Prefetcher(VolumeCache& cache);
    ~Prefetcher();

    // Not copyable or movable (owns a thread).
    Prefetcher(const Prefetcher&) = delete;
    Prefetcher& operator=(const Prefetcher&) = delete;

    /// Request prefetching of volumes at the given file paths.
    /// Any previously queued (but not yet started) work is cancelled.
    /// Paths that are already in the cache are skipped.
    void requestPrefetch(const std::vector<std::string>& paths);

    /// Cancel any in-flight or pending prefetch work.
    void cancelPending();

    /// Signal the background thread to exit and join it.
    /// Safe to call multiple times.
    void shutdown();

private:
    void workerLoop();

    VolumeCache& cache_;

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;

    /// Paths the worker should load next.  Protected by mutex_.
    std::vector<std::string> pendingPaths_;

    /// Set to true when new paths are available or shutdown is requested.
    bool hasWork_ = false;

    /// Checked between individual volume loads to abort early.
    std::atomic<bool> cancelFlag_{false};

    /// Set to true to tell the worker to exit.
    std::atomic<bool> exitFlag_{false};
};
