#pragma once

#include <string>
#include <vector>

class VolumeCache;

/// Eager prefetcher for QC mode.  Queues volume paths for adjacent QC rows
/// and loads them into the shared VolumeCache on the **main thread** so that
/// row switches are instant (cache hit) rather than blocking on disk I/O.
///
/// File loading happens synchronously on the main thread because neither
/// libminc nor HDF5 is thread-safe; background loading caused segfaults
/// when accessing MINC files over NFS.
///
/// Usage:
///   1. Construct with a reference to the shared VolumeCache.
///   2. After each row switch, call requestPrefetch() with the paths for
///      the neighbouring rows (prev + next).
///   3. Call loadPending() once per frame from the main loop.  It loads
///      at most one volume per call to avoid stalling the UI.
class Prefetcher {
public:
    explicit Prefetcher(VolumeCache& cache);
    ~Prefetcher() = default;

    // Not copyable or movable.
    Prefetcher(const Prefetcher&) = delete;
    Prefetcher& operator=(const Prefetcher&) = delete;

    /// Queue paths for prefetching.  Replaces any previously queued paths.
    /// Paths already present in the cache will be skipped at load time.
    void requestPrefetch(const std::vector<std::string>& paths);

    /// Cancel any pending (not yet loaded) prefetch work.
    void cancelPending();

    /// Load at most one queued volume into the cache.
    /// Call this once per frame from the main loop.
    /// Returns true if a volume was loaded (or skipped), false if the
    /// queue is empty.
    bool loadPending();

private:
    VolumeCache& cache_;

    /// Paths remaining to be loaded.
    std::vector<std::string> pendingPaths_;
};
