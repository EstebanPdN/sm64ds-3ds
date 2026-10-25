#pragma once
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace integrated_storage {
// Settings are read before the retail heap (and its global operator new)
// exists. Use bounded stack paths for recovery and transactions on both
// console models. libc FILE allocation uses the independent C heap.
constexpr size_t path_capacity = 1024;
inline bool sibling_path(char (&out)[path_capacity], const char* path, const char* suffix) {
    const size_t suffix_size = std::strlen(suffix);
    size_t n = 0;
    while (n < path_capacity && path[n]) ++n;
    if (n >= path_capacity - suffix_size) { errno = ENAMETOOLONG; return false; }
    std::memcpy(out, path, n);
    std::memcpy(out + n, suffix, suffix_size + 1);
    return true;
}

// Save/settings operations run on their serialized game-side owner, never
// in the realtime audio pump. Sibling .tmp/.bak names are transaction state.
// A successful return verifies write/flush/fsync/close and final rename. It
// does not promise SD-controller or directory durability on sudden power loss.
struct FileOps {
    // libctru 2.7 archive_rename deletes an existing destination and retries.
    // Its final EIO can therefore mean the destination has already vanished.
    // Never pass an existing primary as a rename destination on that target.
#if defined(__3DS__)
    static constexpr bool backup_first = true;
#else
    static constexpr bool backup_first = false;
#endif
    static FILE* open(const char* p, const char* mode) { return std::fopen(p, mode); }
    static size_t write(const void* p, size_t n, FILE* f) { return std::fwrite(p, 1, n, f); }
    static int flush(FILE* f) { return std::fflush(f); }
    static int sync(FILE* f) { return ::fsync(::fileno(f)); }
    static int close(FILE* f) { return std::fclose(f); }
    static int rename(const char* a, const char* b) { return std::rename(a, b); }
    static int remove(const char* p) { return std::remove(p); }
    static int exists(const char* p) { struct stat s; return ::stat(p, &s); }
};

// If a non-replacing filesystem was interrupted between its two renames,
// the old complete image is still in .bak. Never treat another I/O error as
// a missing save, and never replace or discard an existing .bak implicitly.
template<class Ops = FileOps>
FILE* open_read(const char* path) {
    if (!path || !*path) { errno = EINVAL; return nullptr; }
    FILE* file = Ops::open(path, "rb");
    if (file || errno != ENOENT) return file;
    char backup[path_capacity];
    if (!sibling_path(backup, path, ".bak")) return nullptr;
    return Ops::open(backup, "rb");
}

template<class Ops = FileOps>
bool replace_file(const char* path, const void* bytes, size_t count) {
    if (!path || !*path || (!bytes && count)) { errno = EINVAL; return false; }
    char temporary[path_capacity], backup[path_capacity];
    if (!sibling_path(temporary, path, ".tmp") || !sibling_path(backup, path, ".bak")) return false;
    bool primary_exists = Ops::exists(path) == 0;
    if (!primary_exists) {
        if (errno != ENOENT) return false;
        if (Ops::exists(backup) == 0) {
            if (Ops::rename(backup, path) != 0) return false;
            primary_exists = true;
        } else if (errno != ENOENT) return false;
    }
    FILE* file = Ops::open(temporary, "wb");
    if (!file) return false;
    bool good = Ops::write(bytes, count, file) == count;
    if (Ops::flush(file) != 0) good = false;
    if (good && Ops::sync(file) != 0) good = false;
    if (Ops::close(file) != 0) good = false;
    if (!good) { Ops::remove(temporary); return false; }
    if (!primary_exists || !Ops::backup_first) {
        if (Ops::rename(temporary, path) == 0) return true;
        const int rename_error = errno;
        // Host POSIX replacement is preferred. On 3DS an existing primary
        // always takes backup-first, before libctru can unlink that image.
        if (!primary_exists || (rename_error != EEXIST && rename_error != ENOTEMPTY)) {
            Ops::remove(temporary); errno = rename_error; return false;
        }
    }
    if (Ops::exists(backup) == 0 || errno != ENOENT) {
        Ops::remove(temporary); return false;
    }
    if (Ops::rename(path, backup) != 0) {
        Ops::remove(temporary); return false;
    }
    if (Ops::rename(temporary, path) == 0) {
        // Failure to clean the old image does not invalidate the new image.
        // A later non-replacing save refuses to overwrite that backup.
        Ops::remove(backup);
        return true;
    }
    const int commit_error = errno;
    Ops::rename(backup, path); // If rollback fails, open_read uses .bak.
    Ops::remove(temporary);
    errno = commit_error;
    return false;
}
} // namespace integrated_storage
