// BUILD139: write-behind for the cart save (hal/save_cart.cpp, patched by
// tools/prepare_integrated_save.py). The console spent hundreds of
// milliseconds per SaveDataToCart on the game thread (read the 8 KiB image,
// then write .tmp, sync and rename): the save box after the key ran at
// 12 FPS. The game thread now hands the finished image here and continues;
// this thread performs the same checked replacement (with its .bak
// recovery) for the newest image, so consecutive saves are coalesced.
// save_cart.cpp cannot include <3ds.h> (the game defines u32 itself),
// hence this separate translation unit with a C interface.
#include "../atomic_file.hpp"
#include <3ds.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr unsigned kMaxBytes = 8192;
struct Writer {
    LightLock lock; LightEvent wake, idle; Thread thread = nullptr;
    bool ready = false, failed = false;
    char path[128] = {}; unsigned char data[kMaxBytes] = {}; unsigned size = 0;
    unsigned generation = 0, written = 0;
};
Writer g;
void writer_main(void*) {
    static char path[128]; static unsigned char copy[kMaxBytes];
    for (;;) {
        LightEvent_Wait(&g.wake);
        for (;;) {
            LightLock_Lock(&g.lock);
            const unsigned generation = g.generation, size = g.size;
            if (generation == g.written) { LightEvent_Signal(&g.idle); LightLock_Unlock(&g.lock); break; }
            std::memcpy(path, g.path, sizeof path); std::memcpy(copy, g.data, size);
            LightLock_Unlock(&g.lock);
            bool ok = false;
            for (int attempt = 0; attempt < 3 && !ok; ++attempt) ok = integrated_storage::replace_file(path, copy, size);
            if (!ok) std::printf("Save write-behind: SD write failed (generation %u)\n", generation);
            LightLock_Lock(&g.lock); g.written = generation; LightLock_Unlock(&g.lock);
        }
    }
}
void flush_at_exit();
}

// Waits (at most 3 s) until the newest image is on the SD card.
extern "C" void sm64ds_save_flush() {
    if (g.ready && g.thread) LightEvent_WaitTimeout(&g.idle, 3000000000LL);
}
namespace { void flush_at_exit() { sm64ds_save_flush(); } }

// Queues a copy of the image; 0 means "not queued, write it yourself".
extern "C" int sm64ds_save_write_behind(const char* path, const void* data, unsigned size) {
    if (!path || !data || !size || size > kMaxBytes || std::strlen(path) >= sizeof g.path) return 0;
    if (!g.ready) {
        LightLock_Init(&g.lock); LightEvent_Init(&g.wake, RESET_ONESHOT);
        LightEvent_Init(&g.idle, RESET_STICKY); LightEvent_Signal(&g.idle); g.ready = true;
    }
    if (!g.thread && !g.failed) {
        // Mostly blocked in FS IPC: above the game (0x32) it costs little CPU.
        // BUILD142 New 3DS profile: SD writes on core 1, off the game's core.
        bool is_new = false; APT_CheckNew3DS(&is_new);
        if (is_new) g.thread = threadCreate(writer_main, nullptr, 16 * 1024, 0x31, 1, true);
        if (!g.thread) g.thread = threadCreate(writer_main, nullptr, 16 * 1024, 0x31, -2, true);
        if (g.thread) std::atexit(flush_at_exit); else g.failed = true;
    }
    if (!g.thread) return 0;
    LightLock_Lock(&g.lock);
    std::strcpy(g.path, path); std::memcpy(g.data, data, size); g.size = size; ++g.generation;
    LightEvent_Clear(&g.idle);
    LightLock_Unlock(&g.lock);
    LightEvent_Signal(&g.wake);
    return 1;
}

// Development probe (.3dsx env.txt SM64DS_DEV_SAVE_PROBE=1): three quick
// saves coalesce, the flush waits, and the file must hold the newest image.
extern "C" void sm64ds_save_writer_probe() {
    if (!std::getenv("SM64DS_DEV_SAVE_PROBE")) return;
    static unsigned char image[kMaxBytes];
    const char* path = "sdmc:/3ds/sm64ds/save-probe.sav";
    for (unsigned pass = 1; pass <= 3; ++pass) {
        for (unsigned i = 0; i < kMaxBytes; ++i) image[i] = static_cast<unsigned char>(i * 7 + pass);
        if (!sm64ds_save_write_behind(path, image, kMaxBytes)) { std::printf("Save probe: not queued\n"); return; }
    }
    const auto start = svcGetSystemTick();
    sm64ds_save_flush();
    const double ms = double(svcGetSystemTick() - start) * 1000.0 / double(SYSCLOCK_ARM11);
    static unsigned char back[kMaxBytes];bool same = false;
    if (FILE* f = std::fopen(path, "rb")) { same = std::fread(back, 1, kMaxBytes, f) == kMaxBytes && std::memcmp(back, image, kMaxBytes) == 0; std::fclose(f); }
    std::printf("Save probe: newest image on SD=%u flushMs=%.1f generations=%u written=%u\n", unsigned(same), ms, g.generation, g.written);
}
