#pragma once
#include <string>
#include <vector>

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#endif

/// Hint the OS to read `path` into the page cache asynchronously.
/// Returns immediately; no-op on non-Linux platforms.
inline void os_prefetch_file(const std::string& path)
{
#ifdef __linux__
    int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd >= 0) {
        posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED);
        close(fd);
    }
#else
    (void)path;
#endif
}

inline void os_prefetch_files(const std::vector<std::string>& paths)
{
    for (const auto& p : paths)
        os_prefetch_file(p);
}
