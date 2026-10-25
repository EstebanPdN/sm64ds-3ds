#include "sm64ds/native_platform.hpp"

#include <3ds.h>

namespace sm64ds {
namespace {
constexpr std::uint32_t kVBlank = 1;
constexpr std::uint32_t kIrqMask = (1u << 22) - 1;
constexpr std::uint16_t kGameThreadBit = 1;

struct Platform {
    NativePlatformLog log{};
    NativePlatformStats stats{};
    NativeIrqHandler handlers[22]{};
    std::uint16_t* sleep_queue{};
    std::uint64_t tick_epoch{};
    std::uint64_t wait_until_vblank{};
    std::uint32_t cpsr_mask{0x80};
    bool master{};
    bool dispatching{};
    bool announced_audio{};
};
Platform g;

void dispatch() {
    if (!g.master || (g.cpsr_mask & 0x80) || g.dispatching) return;
    g.dispatching = true;
    // Snapshot this edge. A handler raising the same IRQ does not spin here;
    // the new pending bit survives for the next explicit dispatch boundary.
    const std::uint32_t ready = g.stats.pending_irqs & g.stats.enabled_irqs;
    for (unsigned bit = 0; bit < 22; ++bit) {
        const std::uint32_t mask = 1u << bit;
        if (!(ready & mask) || !g.handlers[bit]) continue;
        if (!g.master || (g.cpsr_mask & 0x80)) break;
        if (!(g.stats.enabled_irqs & mask)) continue;
        g.stats.pending_irqs &= ~mask;
        ++g.stats.irq_dispatches;
        g.handlers[bit]();
    }
    g.dispatching = false;
}
}  // namespace

void native_platform_init(NativePlatformLog log) {
    g = Platform{};
    g.log = log;
    g.tick_epoch = svcGetSystemTick();
    if (log) log("native platform: single context; audio unavailable; physical verification pending");
}

void native_platform_advance_vblank() {
    ++g.stats.vblanks;
    native_platform_irq_raise(kVBlank);
}

bool native_platform_is_waiting() {
    if (g.sleep_queue && ((*g.sleep_queue & kGameThreadBit) == 0))
        g.sleep_queue = nullptr;
    return g.sleep_queue || g.stats.vblanks < g.wait_until_vblank;
}

bool native_platform_ok() { return g.stats.okay; }

NativePlatformStats native_platform_stats() {
    auto result = g.stats;
    result.waiting = native_platform_is_waiting();
    return result;
}

void native_platform_fail(const char* operation) {
    g.stats.okay = false;
    if (g.log) g.log(operation);
}

void native_platform_wait_queue(std::uint16_t* queue) {
    ++g.stats.queue_sleeps;
    if (!queue) {
        native_platform_fail("native platform: unqueued thread sleep needs an explicit service adapter");
        return;
    }
    if (native_platform_is_waiting()) {
        native_platform_fail("native platform: nested sleep in split game loop");
        return;
    }
    *queue = static_cast<std::uint16_t>(*queue | kGameThreadBit);
    g.sleep_queue = queue;
}

void native_platform_wake_queue(std::uint16_t* queue) {
    if (!queue) return;
    // The ROM queue is a halfword of waiter bits; waking clears all of them.
    if (*queue) {
        *queue = 0;
        ++g.stats.queue_wakes;
    }
    if (g.sleep_queue == queue) g.sleep_queue = nullptr;
}

void native_platform_wait_vblank() {
    if (native_platform_is_waiting()) {
        native_platform_fail("native platform: nested VBlank wait in split game loop");
        return;
    }
    g.wait_until_vblank = g.stats.vblanks + 1;
}

std::uint32_t native_platform_irq_disable() {
    const auto old = g.cpsr_mask & 0x80;
    g.cpsr_mask |= 0x80;
    return old;
}

std::uint32_t native_platform_irq_enable() {
    const auto old = g.cpsr_mask & 0x80;
    g.cpsr_mask &= ~0x80u;
    return old;
}

std::uint32_t native_platform_irq_restore(std::uint32_t previous) {
    const auto old = g.cpsr_mask & 0x80;
    g.cpsr_mask = (g.cpsr_mask & ~0x80u) | (previous & 0x80);
    return old;
}

std::uint32_t native_platform_irq_disable_all() {
    const auto old = g.cpsr_mask & 0xc0;
    g.cpsr_mask |= 0xc0;
    return old;
}

std::uint32_t native_platform_irq_restore_all(std::uint32_t previous) {
    const auto old = g.cpsr_mask & 0xc0;
    g.cpsr_mask = (g.cpsr_mask & ~0xc0u) | (previous & 0xc0);
    return old;
}

std::uint32_t native_platform_irq_enable_mask(std::uint32_t mask) {
    const auto old = g.stats.enabled_irqs;
    g.stats.enabled_irqs |= mask & kIrqMask;
    return old;
}

std::uint32_t native_platform_irq_disable_mask(std::uint32_t mask) {
    const auto old = g.stats.enabled_irqs;
    g.stats.enabled_irqs &= ~mask;
    return old;
}

std::uint32_t native_platform_irq_set_mask(std::uint32_t mask) {
    const auto old = g.stats.enabled_irqs;
    g.stats.enabled_irqs = mask & kIrqMask;
    return old;
}

std::uint32_t native_platform_irq_clear(std::uint32_t mask) {
    const auto old = g.stats.pending_irqs;
    g.stats.pending_irqs &= ~mask;
    return old;
}

void native_platform_irq_set_handler(std::uint32_t mask, NativeIrqHandler handler) {
    for (unsigned bit = 0; bit < 22; ++bit)
        if (mask & (1u << bit)) g.handlers[bit] = handler;
}

NativeIrqHandler native_platform_irq_get_handler(std::uint32_t mask) {
    for (unsigned bit = 0; bit < 22; ++bit)
        if (mask & (1u << bit)) return g.handlers[bit];
    return nullptr;
}

void native_platform_irq_set_master(bool enabled) { g.master = enabled; }

void native_platform_irq_raise(std::uint32_t mask) {
    g.stats.pending_irqs |= mask & kIrqMask;
    dispatch();
}

bool native_platform_in_irq() { return g.dispatching; }
std::uint64_t native_platform_ticks() { return (svcGetSystemTick() - g.tick_epoch) / 512; }
std::uint16_t native_platform_boot_indicator() { return 0; }
bool native_platform_audio_available() { return false; }

void native_platform_defer_audio(const char* operation) {
    ++g.stats.deferred_audio_calls;
    if (!g.announced_audio && g.log) {
        g.log(operation);
        g.announced_audio = true;
    }
}
}  // namespace sm64ds
