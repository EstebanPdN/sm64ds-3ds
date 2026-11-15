// Game/mixer lock handoff of the generated sound_native.cpp Guard
// (prepare_integrated_audio.patch_native): exclusion, recursion, the game's
// announcement seen by the mixer, timed contended waits and no deadlock.
// run.py extracts the generated Guard into guard_extract.inc; built with TSan.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <thread>
#include <time.h>

using u64 = uint64_t;
struct RecursiveLock { pthread_mutex_t mutex; };
static void RecursiveLock_Init(RecursiveLock* lock) {
    pthread_mutexattr_t attributes;
    pthread_mutexattr_init(&attributes);
    pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&lock->mutex, &attributes);
}
// libctru: zero on success.
static int RecursiveLock_TryLock(RecursiveLock* lock) { return pthread_mutex_trylock(&lock->mutex); }
static void RecursiveLock_Lock(RecursiveLock* lock) { pthread_mutex_lock(&lock->mutex); }
static void RecursiveLock_Unlock(RecursiveLock* lock) { pthread_mutex_unlock(&lock->mutex); }
static u64 svcGetSystemTick() {
    return u64(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) * 268 / 1000;
}
static void svcSleepThread(long long ns) { std::this_thread::sleep_for(std::chrono::nanoseconds(ns)); }
static volatile int sm64ds_sound_game_waiting = 0;
static thread_local bool on_worker = false;
static bool sm64ds_on_present_worker() { return on_worker; }
#include "audio_diagnostics.hpp"
namespace {
#include "guard_extract.inc"
}

static RecursiveLock lock;
static std::atomic<int> inside{0};
static std::atomic<bool> violation{false}, game_seen{false};
static void critical(int spin_us) {
    if (inside.fetch_add(1) != 0) violation = true;
    const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(spin_us);
    while (std::chrono::steady_clock::now() < until) {}
    inside.fetch_sub(1);
}

int main() {
    RecursiveLock_Init(&lock);
    std::atomic<bool> done{false};
    std::thread watchdog([&] {
        for (int i = 0; i < 400 && !done; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!done) { std::fputs("FAIL deadlock: game/mixer lock handoff did not finish in 20 s\n", stderr); std::_Exit(1); }
    });
    // Mixer: the pump's chunk loop (yield to an announced game, render under
    // the lock) plus its own Guard for queue.refresh().
    std::thread mixer([] {
        on_worker = true;
        for (int chunk = 0; chunk < 3000; ++chunk) {
            while (__atomic_load_n(&sm64ds_sound_game_waiting, __ATOMIC_RELAXED)) { game_seen = true; svcSleepThread(50000); }
            RecursiveLock_Lock(&lock); critical(40); RecursiveLock_Unlock(&lock);
            if (chunk % 8 == 0) { Guard guard(&lock); critical(5); }
        }
    });
    // Game: sound commands, sometimes nested (the lock is recursive).
    for (int command = 0; command < 3000; ++command) {
        Guard guard(&lock);
        critical(10);
        if (command % 16 == 0) { Guard nested(&lock); critical(2); }
        if (command % 4 == 0) std::this_thread::sleep_for(std::chrono::microseconds(30));
    }
    mixer.join();
    done = true;
    watchdog.join();
    const auto& t = enhance::audio_diagnostics::timing;
    const unsigned waits = t.game_waits.load(), max_us = t.game_wait_max_us.load(), total_us = t.game_wait_us.load();
    if (violation) { std::puts("FAIL two threads inside the mixer lock"); return 1; }
    if (!waits || waits > 3000 + 3000 / 16) { std::printf("FAIL contended game waits=%u\n", waits); return 1; }
    if (!max_us || total_us < max_us) { std::printf("FAIL wait timing total=%u max=%u\n", total_us, max_us); return 1; }
    if (!game_seen) { std::puts("FAIL the mixer never saw the game's announcement"); return 1; }
    if (sm64ds_sound_game_waiting != 0) { std::puts("FAIL announcement counter left non-zero"); return 1; }
    std::printf("PASS mixer lock: exclusion over 3000+3000 sections, recursion, %u timed game waits (max %u us), announcement honoured, no deadlock\n",
                waits, max_us);
    return 0;
}
