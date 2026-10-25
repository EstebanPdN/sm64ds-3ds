#pragma once
// Numbered diagnostic dump folders, the same layout as the other EstebanPdN
// ports: <root>/000-dump-YYYYMMDD-HHMMSS, 001-dump-..., and so on.
// dump-sequence.txt keeps the numbering across restarts and clock changes;
// an emptied dumps folder starts again at 000. Portable: host tests use it.
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace dump_folder {
constexpr const char* game_directory = "sdmc:/3ds/sm64ds";
constexpr const char* root_directory = "sdmc:/3ds/sm64ds/dumps";
constexpr const char* sequence_file = "dump-sequence.txt";
constexpr size_t max_path = 160;

// "NNN-dump-YYYYMMDD-HHMMSS" with three or more digits.
inline bool numbered(const char* name, uint32_t& number) {
    const char* p = name;
    uint64_t value = 0;
    unsigned digits = 0;
    while (*p >= '0' && *p <= '9') { value = value * 10 + unsigned(*p - '0'); ++p; ++digits; if (value > UINT32_MAX) return false; }
    if (digits < 3 || std::strncmp(p, "-dump-", 6)) return false;
    p += 6;
    for (unsigned i = 0; i < 15; ++i) {
        if (i == 8 ? p[i] != '-' : (p[i] < '0' || p[i] > '9')) return false;
    }
    if (p[15]) return false;
    number = uint32_t(value);
    return true;
}

inline bool valid_stamp(const char* stamp) {
    if (!stamp || std::strlen(stamp) != 15 || stamp[8] != '-') return false;
    for (unsigned i = 0; i < 15; ++i) if (i != 8 && (stamp[i] < '0' || stamp[i] > '9')) return false;
    return true;
}

inline uint32_t read_sequence(const char* root) {
    char path[max_path];
    if (std::snprintf(path, sizeof path, "%s/%s", root, sequence_file) >= int(sizeof path)) return 0;
    FILE* f = std::fopen(path, "rb");
    if (!f) return 0;
    char text[32]{};
    uint32_t next = 0;
    if (std::fgets(text, sizeof text, f)) {
        char* end = nullptr;
        errno = 0;
        const unsigned long value = std::strtoul(text, &end, 10);
        if (!errno && text[0] >= '0' && text[0] <= '9' && end != text && (*end == '\n' || !*end) && value < UINT32_MAX)
            next = uint32_t(value);
    }
    std::fclose(f);
    return next;
}

inline void write_sequence(const char* root, uint32_t next) {
    char path[max_path], temporary[max_path + 4];
    if (std::snprintf(path, sizeof path, "%s/%s", root, sequence_file) >= int(sizeof path)) return;
    std::snprintf(temporary, sizeof temporary, "%s.tmp", path);
    FILE* f = std::fopen(temporary, "wb");
    if (!f) return;
    const bool ok = std::fprintf(f, "%lu\n", (unsigned long)next) > 0;
    if (std::fclose(f) != 0 || !ok) { std::remove(temporary); return; }
    std::remove(path);
    std::rename(temporary, path);
}

// Creates the next numbered folder for a "YYYYMMDD-HHMMSS" stamp.
inline bool create_at(const char* root, const char* stamp, char* out, size_t out_size) {
    if (!out || !out_size) return false;
    out[0] = 0;
    if (!root || !valid_stamp(stamp)) return false;
    if (::mkdir(root, 0777) && errno != EEXIST) return false;
    DIR* dir = ::opendir(root);
    if (!dir) return false;
    uint32_t next = read_sequence(root);
    bool any = false;
    while (const dirent* entry = ::readdir(dir)) {
        uint32_t number;
        if (!numbered(entry->d_name, number)) continue;
        any = true;
        if (number == UINT32_MAX) { ::closedir(dir); return false; }
        if (number >= next) next = number + 1;
    }
    ::closedir(dir);
    if (!any) next = 0;
    for (unsigned attempt = 0; attempt < 1000 && next != UINT32_MAX; ++attempt, ++next) {
        const int n = std::snprintf(out, out_size, "%s/%03lu-dump-%s", root, (unsigned long)next, stamp);
        if (n < 0 || size_t(n) >= out_size) break;
        if (::mkdir(out, 0777) == 0) { write_sequence(root, next + 1); return true; }
        if (errno != EEXIST) break;
    }
    out[0] = 0;
    return false;
}

inline bool stamp_now(char (&stamp)[32]) {
    const time_t now = std::time(nullptr);
    const struct tm* calendar = std::localtime(&now);
    return calendar && std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", calendar) == 15;
}

inline bool create(char* out, size_t out_size, const char* root = root_directory) {
    char stamp[32];
    if (!stamp_now(stamp)) { if (out && out_size) out[0] = 0; return false; }
    return create_at(root, stamp, out, out_size);
}

struct DeleteResult { unsigned removed = 0, failed = 0; };

// Removes regular files directly inside one dump folder, then the folder.
inline void remove_folder(const char* path, DeleteResult& result) {
    DIR* dir = ::opendir(path);
    if (!dir) { ++result.failed; return; }
    while (const dirent* entry = ::readdir(dir)) {
        if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;
        char file[max_path + 64];
        if (std::snprintf(file, sizeof file, "%s/%s", path, entry->d_name) >= int(sizeof file)) { ++result.failed; continue; }
        struct stat info{};
        if (::lstat(file, &info) != 0 || !S_ISREG(info.st_mode)) { ++result.failed; continue; }
        if (std::remove(file) == 0) ++result.removed; else ++result.failed;
    }
    ::closedir(dir);
    if (::rmdir(path) != 0) ++result.failed;
}

// Every numbered dump folder and the sequence counter. Other files stay.
inline DeleteResult delete_all(const char* root = root_directory) {
    DeleteResult result;
    DIR* dir = ::opendir(root);
    if (!dir) return result;
    char folders[64][max_path];
    unsigned count = 0;
    bool more = true;
    for (unsigned round = 0; more && round < 64; ++round) {
        count = 0;
        more = false;
        ::rewinddir(dir);
        while (const dirent* entry = ::readdir(dir)) {
            uint32_t number;
            if (!numbered(entry->d_name, number)) continue;
            if (count == 64) { more = true; break; }
            if (std::snprintf(folders[count], max_path, "%s/%s", root, entry->d_name) < int(max_path)) ++count;
            else ++result.failed;
        }
        for (unsigned i = 0; i < count; ++i) remove_folder(folders[i], result);
    }
    ::closedir(dir);
    char path[max_path];
    if (std::snprintf(path, sizeof path, "%s/%s", root, sequence_file) < int(sizeof path) && std::remove(path) == 0) ++result.removed;
    return result;
}

// 24-bit bottom-up BMP header; rows are padded to four bytes.
inline unsigned bmp_row_bytes(unsigned width) { return (width * 3 + 3) & ~3u; }
inline void bmp_header(uint8_t (&out)[54], unsigned width, unsigned height) {
    const uint32_t image = bmp_row_bytes(width) * height, file = 54 + image;
    std::memset(out, 0, sizeof out);
    out[0] = 'B'; out[1] = 'M';
    auto put32 = [&](unsigned at, uint32_t v) { for (unsigned i = 0; i < 4; ++i) out[at + i] = uint8_t(v >> (8 * i)); };
    put32(2, file); put32(10, 54); put32(14, 40); put32(18, width); put32(22, height);
    out[26] = 1; out[28] = 24; put32(34, image); put32(38, 2835); put32(42, 2835);
}
}
