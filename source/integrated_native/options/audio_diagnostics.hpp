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
} }
