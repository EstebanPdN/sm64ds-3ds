#pragma once

#include <cstdint>

namespace sm64ds {

using NativePlatformLog = void (*)(const char* message);
using NativeIrqHandler = void (*)();

struct NativePlatformStats {
    std::uint64_t vblanks{};
    std::uint64_t irq_dispatches{};
    std::uint64_t queue_sleeps{};
    std::uint64_t queue_wakes{};
    std::uint64_t deferred_audio_calls{};
    std::uint32_t enabled_irqs{};
    std::uint32_t pending_irqs{};
    bool waiting{};
    bool okay{true};
};

// A single game context on the application core. The caller splits the ROM
// loop at its explicit phase-7 wait; no second OS thread or DS scheduler runs.
// Call init before the ROM's IRQ registration and before any game step.
void native_platform_init(NativePlatformLog log = nullptr);
void native_platform_advance_vblank();
bool native_platform_is_waiting();
bool native_platform_ok();
NativePlatformStats native_platform_stats();

// Split-loop waits: the caller MUST return to presentation after this call,
// and MUST NOT execute the frame tail until is_waiting() becomes false.
// This is not a replacement for arbitrary blocking SDK waits elsewhere.
void native_platform_wait_queue(std::uint16_t* queue);
void native_platform_wake_queue(std::uint16_t* queue);
void native_platform_wait_vblank();

// IRQ state belongs to the game; the process never writes privileged CPSR or
// a DS bus address. Handler callbacks execute on the presentation/game thread.
std::uint32_t native_platform_irq_disable();
std::uint32_t native_platform_irq_enable();
std::uint32_t native_platform_irq_restore(std::uint32_t previous);
std::uint32_t native_platform_irq_disable_all();
std::uint32_t native_platform_irq_restore_all(std::uint32_t previous);
std::uint32_t native_platform_irq_enable_mask(std::uint32_t mask);
std::uint32_t native_platform_irq_disable_mask(std::uint32_t mask);
std::uint32_t native_platform_irq_set_mask(std::uint32_t mask);
std::uint32_t native_platform_irq_clear(std::uint32_t mask);
void native_platform_irq_set_handler(std::uint32_t mask, NativeIrqHandler handler);
NativeIrqHandler native_platform_irq_get_handler(std::uint32_t mask);
void native_platform_irq_set_master(bool enabled);
void native_platform_irq_raise(std::uint32_t mask);
bool native_platform_in_irq();

// The original OS timer ticks at DS bus clock/64. CTR system tick is exactly
// eight times the DS bus clock, so /512 keeps the original timer units.
std::uint64_t native_platform_ticks();
std::uint16_t native_platform_boot_indicator();

// Audio is an explicit incomplete backend. Boot/game adapters may defer sound
// work and log that decision, but must never report playback as available.
bool native_platform_audio_available();
void native_platform_defer_audio(const char* operation);
void native_platform_fail(const char* operation);

}  // namespace sm64ds
