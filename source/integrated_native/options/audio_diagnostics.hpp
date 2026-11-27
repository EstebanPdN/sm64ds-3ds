#pragma once
#include <atomic>
#include <cstdint>

namespace enhance { namespace audio_diagnostics {
// The pump is the sole writer. Readers may sample from another thread;
// individual relaxed counters are coherent, the whole snapshot is advisory.
// No printing, file I/O, allocation or lock occurs in the audio callback.
struct Snapshot {
    uint32_t submitted = 0, completed = 0, queue_drains = 0;
    uint32_t clipped_samples = 0, peak_input = 0, peak_output = 0;
    uint32_t detailed_enabled = 0;
};
struct Counters {
    std::atomic<uint32_t> submitted{0}, completed{0}, queue_drains{0};
    std::atomic<uint32_t> clipped_samples{0}, peak_input{0}, peak_output{0};
    std::atomic<uint32_t> detailed_enabled{0};
};
static_assert(std::atomic<uint32_t>::is_always_lock_free, "Audio counters require lock-free 32-bit stores");
inline Counters counters;
inline void publish(const Snapshot& s) {
    counters.submitted.store(s.submitted, std::memory_order_relaxed);
    counters.completed.store(s.completed, std::memory_order_relaxed);
    counters.queue_drains.store(s.queue_drains, std::memory_order_relaxed);
    counters.clipped_samples.store(s.clipped_samples, std::memory_order_relaxed);
    counters.peak_input.store(s.peak_input, std::memory_order_relaxed);
    counters.peak_output.store(s.peak_output, std::memory_order_relaxed);
    counters.detailed_enabled.store(s.detailed_enabled, std::memory_order_relaxed);
}
inline Snapshot snapshot() {
    return {counters.submitted.load(std::memory_order_relaxed),
            counters.completed.load(std::memory_order_relaxed),
            counters.queue_drains.load(std::memory_order_relaxed),
            counters.clipped_samples.load(std::memory_order_relaxed),
            counters.peak_input.load(std::memory_order_relaxed),
            counters.peak_output.load(std::memory_order_relaxed),
            counters.detailed_enabled.load(std::memory_order_relaxed)};
}
// A full drain is an observed exhaustion of a previously submitted queue,
// not a measurement of missing samples or proof of an audible dropout.
inline bool queue_drained(unsigned previously_owned, unsigned completed_now) {
    return previously_owned != 0 && completed_now == previously_owned;
}

// Mixer timing for the test reports (wall time, microseconds; the counters
// wrap and readers take differences). Writers: the audio pump (pump, mix,
// wait, chunks, chunk_max) and the game thread (game_*). The report resets
// the maxima with exchange(0); a racing update may survive one report.
struct Timing {
    std::atomic<uint32_t> pump_us{0}, mix_us{0}, wait_us{0}, chunks{0}, chunk_max_us{0};
    std::atomic<uint32_t> game_wait_us{0}, game_waits{0}, game_wait_max_us{0};
    // BUILD134: waits after the image was handed to the renderer (the game
    // thread would be parked anyway) and command batches deferred to there.
    std::atomic<uint32_t> late_wait_us{0}, late_waits{0}, deferred_batches{0};
    // BUILD137: real underruns, counted when a buffer is added to a channel
    // that already stopped (every queued buffer played: audible silence).
    // queue_drained (Snapshot) misses the gaps that open during a refill.
    std::atomic<uint32_t> underruns{0};
};
inline Timing timing;
// ARM11 system ticks (268.111856 MHz) to microseconds: x * 3911 / 2^20.
inline uint32_t ticks_to_us(uint64_t ticks) { return uint32_t((ticks * 3911u) >> 20); }
inline void raise_max(std::atomic<uint32_t>& max, uint32_t value) {
    if (value > max.load(std::memory_order_relaxed)) max.store(value, std::memory_order_relaxed);
}
// One render chunk: waiting for the game (yield + lock), then mixing under the lock.
inline void record_chunk(uint64_t wait_ticks, uint64_t mix_ticks) {
    const uint32_t mix = ticks_to_us(mix_ticks);
    timing.wait_us.fetch_add(ticks_to_us(wait_ticks), std::memory_order_relaxed);
    timing.mix_us.fetch_add(mix, std::memory_order_relaxed);
    timing.chunks.fetch_add(1, std::memory_order_relaxed);
    raise_max(timing.chunk_max_us, mix);
}
inline void record_pump(uint64_t ticks) { timing.pump_us.fetch_add(ticks_to_us(ticks), std::memory_order_relaxed); }
// The game thread waited for the mixer's lock (contended commands only).
inline void record_game_wait(uint64_t ticks) {
    const uint32_t us = ticks_to_us(ticks);
    timing.game_wait_us.fetch_add(us, std::memory_order_relaxed);
    timing.game_waits.fetch_add(1, std::memory_order_relaxed);
    raise_max(timing.game_wait_max_us, us);
}
inline void record_late_wait(uint64_t ticks) {
    timing.late_wait_us.fetch_add(ticks_to_us(ticks), std::memory_order_relaxed);
    timing.late_waits.fetch_add(1, std::memory_order_relaxed);
}
} }
