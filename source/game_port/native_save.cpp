#include "sm64ds/native_save.hpp"
#include "sm64ds/native_platform.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

// Native replacement of the backup medium and its ARM7 transaction boundary.
// The recovered SaveData checksum, tag, primary/mirror records, formatting and
// slot selection stay untouched. The 0xd01 descriptor in the supplied ROM is
// an 8192-byte EEPROM; a missing file reads as an erased medium, not a valid
// save. The game's own first write creates it in this integration's folder.
namespace {
constexpr std::uint32_t kSize = 0x2000;
constexpr const char* kPath = "sdmc:/3ds/sm64ds/native/save.bin";
sm64ds::NativeSaveStats stats;
std::uint32_t free_locks[2] = {0xffffffffu, 0xffff0000u};

bool error(const char* message) {
    ++stats.failures;
    stats.last_error = message;
    std::fprintf(stderr, "[native-save] %s (errno=%d)\n", message, errno);
    return false;
}
bool bounds(std::uint32_t offset, std::uint32_t size, const void* buffer) {
    return offset <= kSize && size <= kSize - offset && (!size || buffer);
}
bool valid_size(std::FILE* file) {
    return std::fseek(file, 0, SEEK_END) == 0 && std::ftell(file) == long(kSize);
}
bool directories() {
    const char* paths[] = {"sdmc:/3ds", "sdmc:/3ds/sm64ds", "sdmc:/3ds/sm64ds/native"};
    for (const auto* path : paths) {
        if (::mkdir(path, 0777) == 0) continue;
        struct stat st{};
        if (errno != EEXIST || ::stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
            return error("cannot create save directory");
    }
    return true;
}
bool read(std::uint32_t offset, void* destination, std::uint32_t size) {
    ++stats.reads;
    if (!bounds(offset, size, destination)) return error("EEPROM read outside medium");
    auto* file = std::fopen(kPath, "rb");
    if (!file) {
        if (errno != ENOENT) return error("cannot open save for read");
        if (size) std::memset(destination, 0xff, size);
        return true;
    }
    const bool okay = valid_size(file) && std::fseek(file, long(offset), SEEK_SET) == 0 &&
        std::fread(destination, 1, size, file) == size && !std::ferror(file);
    const int closed = std::fclose(file);
    return (okay && closed == 0) || error("save read failed or existing file has wrong size");
}
bool write(std::uint32_t offset, const void* source, std::uint32_t size) {
    ++stats.writes;
    if (!bounds(offset, size, source)) return error("EEPROM write outside medium");
    auto* file = std::fopen(kPath, "r+b");
    if (!file) {
        if (errno != ENOENT) return error("cannot open save for write");
        if (!directories()) return false;
        file = std::fopen(kPath, "w+b");
        if (!file) return error("cannot create save medium");
        std::uint8_t erased[256];
        std::memset(erased, 0xff, sizeof(erased));
        bool okay = true;
        for (unsigned position = 0; position < kSize && okay; position += sizeof(erased))
            okay = std::fwrite(erased, 1, sizeof(erased), file) == sizeof(erased);
        if (!okay) {
            std::fclose(file);
            return error("cannot initialize erased save medium");
        }
        stats.created = true;
    }
    // Existing malformed files are preserved and reported, never silently
    // replaced with a blank save. Write only the range the game requested:
    // the original primary/mirror scheme owns interrupted-record recovery.
    bool okay = valid_size(file) && std::fseek(file, long(offset), SEEK_SET) == 0 &&
        std::fwrite(source, 1, size, file) == size && !std::ferror(file);
    if (okay) okay = std::fflush(file) == 0;
    if (okay) okay = ::fsync(::fileno(file)) == 0;
    const int closed = std::fclose(file);
    return (okay && closed == 0) || error("save program or persistent flush failed");
}
bool verify(std::uint32_t offset, const void* source, std::uint32_t size) {
    ++stats.verifies;
    if (size > 0x100 || !bounds(offset, size, source)) return error("invalid EEPROM verify span");
    std::uint8_t actual[0x100];
    if (!read(offset, actual, size)) return false;
    return !std::memcmp(actual, source, size) || error("save verification mismatch");
}

// SDK card-work layout shared by the unchanged read/write chunking functions.
// Its 0xe0 staging buffer contains at most 256 bytes per transaction.
struct CardWork {
    std::uint32_t* command;
    std::uint32_t reserved04;
    std::int32_t owner, depth;
    std::uint16_t wait_queue, reserved12;
    std::int32_t kind;
    std::uint32_t source, destination, length;
    std::int32_t reserved24;
    std::uint32_t callback, callback_arg, worker, state, priority;
    std::uint8_t thread_reserved[0xd0 - 0x3c];
    std::uint32_t thread_id;
    std::uint16_t completion_queue, reserved_d6;
    std::uint8_t reserved_d8[8], staging[0x100];
};
static_assert(offsetof(CardWork, owner) == 8 && offsetof(CardWork, state) == 0x34);
static_assert(offsetof(CardWork, staging) == 0xe0 && sizeof(CardWork) == 0x1e0);
bool work_initialized = false;
}

namespace sm64ds {
NativeSaveStats native_save_stats() { return stats; }
const char* native_save_path() { return kPath; }
}

extern "C" {
extern std::uint8_t data_020a8180[];
extern std::uint32_t data_020a8160[];
extern std::uint32_t* data_020a8760;
void func_02060188(int lock, int kind);
[[noreturn]] void sm64ds_native_missing(const char*);

int func_02057020() {
    const auto previous = sm64ds::native_platform_irq_disable();
    for (unsigned word = 0; word < 2; ++word) {
        if (!free_locks[word]) continue;
        const unsigned bit = __builtin_clz(free_locks[word]);
        free_locks[word] &= ~(0x80000000u >> bit);
        sm64ds::native_platform_irq_restore(previous);
        return int(0x40 + 32 * word + bit);
    }
    sm64ds::native_platform_irq_restore(previous);
    return -3; // Original SDK exhausted-lock-ID result.
}
void func_02057078(std::uint16_t id) {
    if (id < 0x40 || id >= 0x70) sm64ds_native_missing("save: invalid OS lock ID release");
    const auto previous = sm64ds::native_platform_irq_disable();
    const unsigned slot = id - 0x40;
    free_locks[slot / 32] |= 0x80000000u >> (slot % 32);
    sm64ds::native_platform_irq_restore(previous);
}
void func_0205ff80(std::uint16_t lock) {
    if (!work_initialized) {
        if (!data_020a8760 || data_020a8760[0] != 0xd01 || data_020a8760[1] != kSize)
            sm64ds_native_missing("save: ROM backup descriptor is not the supported EEPROM");
        CardWork initial{};
        initial.command = data_020a8160;
        initial.owner = -3;
        initial.reserved24 = -1;
        initial.state = 1;
        initial.priority = 4;
        std::memcpy(data_020a8180, &initial, sizeof(initial));
        std::memset(data_020a8160, 0, 8 * sizeof(std::uint32_t));
        work_initialized = true;
    }
    auto* work = reinterpret_cast<CardWork*>(data_020a8180);
    if (work->owner != -3 && work->owner != lock)
        sm64ds_native_missing("save: simultaneous card owner in the single-context runtime");
    func_02060188(lock, 2); // Original ownership/depth logic.
}

int func_02060f60(void* self, int command, int /* retries */) {
    auto* work = static_cast<CardWork*>(self);
    if (!work || !work->command) { error("missing EEPROM command block"); return 0; }
    auto* block = work->command;
    bool okay = false;
    switch (command) {
    case 0: okay = true; break; // Block pointer accepted by this actual backend.
    case 2: okay = block[1] == 0xd01; break;
    case 6:
        okay = block[5] <= 0x100 && read(block[3], reinterpret_cast<void*>(block[4]), block[5]);
        break;
    case 8:
        okay = block[5] <= 0x100 && write(block[4], reinterpret_cast<const void*>(block[3]), block[5]);
        break;
    case 9:
        okay = verify(block[4], reinterpret_cast<const void*>(block[3]), block[5]);
        break;
    default: error("unsupported EEPROM command"); break;
    }
    block[0] = okay ? 0 : 1;
    return okay ? 1 : 0;
}
} // extern "C"
