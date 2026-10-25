#pragma once
#include <atomic>
#include <cstdint>

namespace enhance_runtime {
// Single game-thread producer and single presentation-thread consumer.
// Hiding an image must not retire the worker's outstanding read lease.
struct OverlayPublication {
    std::atomic<std::uint32_t> published{0}, acked{0};
    std::atomic<int> slot{0};
    std::atomic<bool> visible{false};
    std::uint32_t next = 1;

    bool can_write() const {
        const auto generation = published.load(std::memory_order_acquire);
        return !generation || acked.load(std::memory_order_acquire) == generation;
    }
    void publish() {
        slot.store(1 - slot.load(std::memory_order_relaxed), std::memory_order_relaxed);
        if (++next == 0) ++next; // zero is reserved for an empty channel
        published.store(next, std::memory_order_release);
        visible.store(true, std::memory_order_release);
    }
    void hide() { visible.store(false, std::memory_order_release); }
    bool acquire(std::uint32_t& generation) {
        generation = published.load(std::memory_order_acquire);
        if (generation && visible.load(std::memory_order_acquire)) return true;
        // Only the consumer retires hidden images, after its earlier copies.
        acked.store(generation, std::memory_order_release);
        return false;
    }
    void acknowledge(std::uint32_t generation) {
        acked.store(generation, std::memory_order_release);
    }
    void reset() {
        // Caller has joined the consumer before resetting or freeing buffers.
        hide(); published.store(0); acked.store(0); slot.store(0); next = 1;
    }
};
}
