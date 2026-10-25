#include "sm64ds/native_platform.hpp"
#include <3ds/synchronization.h>
#include <citro3d.h>
#include <cstring>
#include <initializer_list>

// Hardware-facing symbols only. All ordinary game logic remains in the
// recovered sources. Link these instead of their DS hardware implementations.
extern "C" {
[[noreturn]] void sm64ds_native_missing(const char*);
// The original IRQ::VBlankHandler writes the SDK interrupt-check word at
// DTCM+0x3ff8. DS DTCM is 16 KiB; it is not ROM data and is not a one-byte
// placeholder. Link every data_023c0000 reference to this owned ARM11 block.
alignas(32) std::uint8_t data_023c0000[0x4000]{};
static_assert(sizeof(data_023c0000) == 0x4000);

std::uint32_t _ZN3IRQ7DisableEv() { return sm64ds::native_platform_irq_disable(); }
std::uint32_t _ZN3IRQ6EnableEv() { return sm64ds::native_platform_irq_enable(); }
std::uint32_t _ZN3IRQ7RestoreEj(std::uint32_t old) {
    return sm64ds::native_platform_irq_restore(old);
}
std::uint32_t _ZN3IRQ10DisableAllEv() { return sm64ds::native_platform_irq_disable_all(); }
std::uint32_t _ZN3IRQ10RestoreAllEj(std::uint32_t old) {
    return sm64ds::native_platform_irq_restore_all(old);
}
std::uint32_t _ZN3IRQ10EnableIRQsEj(std::uint32_t mask) {
    return sm64ds::native_platform_irq_enable_mask(mask);
}
std::uint32_t _ZN3IRQ11DisableIRQsEj(std::uint32_t mask) {
    return sm64ds::native_platform_irq_disable_mask(mask);
}
std::uint32_t _ZN3IRQ7SetIRQsEj(std::uint32_t mask) {
    return sm64ds::native_platform_irq_set_mask(mask);
}
std::uint32_t _ZN3IRQ15ClearInterruptsEj(std::uint32_t mask) {
    return sm64ds::native_platform_irq_clear(mask);
}
void _ZN3IRQ13SetIRQHandlerEjPFvvE(std::uint32_t mask, sm64ds::NativeIrqHandler handler) {
    sm64ds::native_platform_irq_set_handler(mask, handler);
}
sm64ds::NativeIrqHandler _ZN3IRQ13GetIRQHandlerEj(std::uint32_t mask) {
    return sm64ds::native_platform_irq_get_handler(mask);
}

// These queue adapters implement the sole context used by native_boot's
// explicit frame split. They cannot turn an arbitrary SDK function into a
// coroutine. Additional blocking services must adapt their own call boundary.
void OS_SleepThread(std::uint16_t* queue) { sm64ds::native_platform_wait_queue(queue); }
void OS_WakeupThread(std::uint16_t* queue) { sm64ds::native_platform_wake_queue(queue); }
void VBlankIntrWait() { sm64ds::native_platform_wait_vblank(); }
void IntrWait() { sm64ds::native_platform_wait_vblank(); }
void Halt() { sm64ds::native_platform_wait_vblank(); }
void _ZN4CP1516WaitForInterruptEv() { sm64ds::native_platform_wait_vblank(); }
std::uint32_t ARMProcessorMode() { return sm64ds::native_platform_in_irq() ? 0x12 : 0x1f; }

extern std::uint16_t data_020a6430;
extern std::uint16_t data_020a642c;
extern std::uint32_t data_020a6438[];
extern std::uint32_t data_020a6434;
extern std::uint32_t data_020a6130;
extern std::uint8_t data_020a6134[];
extern std::uint8_t data_020a621c[];
extern void* data_020a612c;
extern void* data_020a6128;

void func_02058308() {
    if (data_020a6130) return;
    // Original thread-manager fields, for the one context that NativeBoot
    // actually drives. There is no fake idle thread, saved ARM9 context or DS
    // stack sentinel: libctru owns the real application stack and context.
    std::memset(data_020a6134, 0, 0x54);
    std::memset(data_020a621c, 0, 0x94);
    const std::uint32_t runnable = 1, priority = 0x10;
    const auto main_thread = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(data_020a621c));
    const std::uint16_t slots = 16;
    std::memcpy(data_020a621c + 0x64, &runnable, 4);
    std::memcpy(data_020a621c + 0x70, &priority, 4);
    std::memcpy(data_020a6134 + 2, &slots, 2);
    for (const unsigned offset : {8u, 12u, 20u})
        std::memcpy(data_020a6134 + offset, &main_thread, 4);
    data_020a612c = data_020a6134 + 8;
    data_020a6128 = nullptr;
    data_020a6130 = 1;
}

void func_02059788() {
    // NativeBoot enters at original main(), after the CRT/pre-main thread
    // initialization in the cartridge. Install that necessary record first.
    func_02058308();
    if (data_020a6430) return;
    data_020a6430 = 1;
    data_020a642c |= 1;
    data_020a6438[0] = data_020a6438[1] = 0;
    data_020a6434 = 0;
    // No DS timer IRQ is necessary: the native 64-bit monotonic clock supplies
    // its extension directly in func_02059650 rather than through overflow IRQs.
}
std::uint64_t func_02059650() { return sm64ds::native_platform_ticks(); }
// PM's shared DS word is a VBlank count, separate from the OS timer. Citro3D's
// existing GSP callback advances this counter even inside a synchronous caller;
// do not replace that callback or substitute the game's presentation counter.
std::uint32_t sm64ds_native_vblank_count() {
    return C3D_FrameCounter(0);
}

extern std::uint8_t data_020a0f10;
extern std::uint16_t data_020a0f1c;
int func_0203d974() {
    if (sm64ds::native_platform_boot_indicator() == 2) return 1;
    return data_020a0f10 != 0;
}
int func_0203d9b4() {
    if (sm64ds::native_platform_boot_indicator() == 2) return 0;
    return data_020a0f10 == 0;
}
int func_0203d7d4(int arg) {
    if (arg) return 1;
    data_020a0f1c |= 0x80;
    return sm64ds::native_platform_boot_indicator() == 2;
}

// Firmware power/audio preference query. The SDK's existing nonzero error
// branch preserves game defaults; claiming success would invent DS firmware.
int func_0205f8e0(int*, int*) { return 2; }

extern void* data_020a7fc8[];
void func_0205ba64(int channel, void* callback) {
    if (channel < 0 || channel >= 32)
        sm64ds_native_missing("PXI: invalid callback channel");
    const auto saved = sm64ds::native_platform_irq_disable();
    data_020a7fc8[channel] = callback;
    sm64ds::native_platform_irq_restore(saved);
}
int func_0205ba3c(int channel, int processor) {
    if (channel < 0 || channel >= 32 || processor < 0 || processor > 1)
        sm64ds_native_missing("PXI: invalid readiness query");
    if (processor == 0) return data_020a7fc8[channel] != nullptr;
    // Returning zero here feeds unbounded SDK spin loops; returning one would
    // claim an ARM7 service exists. Stop at the missing service boundary.
    sm64ds_native_missing("PXI: ARM7 readiness requested; native service adapter missing");
}
int IPCSend(std::uint32_t, std::uint32_t, std::uint32_t) {
    sm64ds_native_missing("PXI: native transaction adapter missing; no ARM7 acknowledgement generated");
}

void func_0205a588(void* destination, std::uint32_t value, std::uint32_t size) {
    // SDK byte fill, preserving the original alignment ladder and register
    // replication even if a caller supplies bits above the nominal byte.
    // CPU byte stores avoid the DS primitive's unnecessary adjacent-byte RMW.
    auto* output = static_cast<std::uint8_t*>(destination);
    if (!size) return;
    if (reinterpret_cast<std::uintptr_t>(output) & 1) {
        *output++ = static_cast<std::uint8_t>(value);
        if (!--size) return;
    }
    if (size >= 2) {
        value |= value << 8;
        if (reinterpret_cast<std::uintptr_t>(output) & 2) {
            const auto half = static_cast<std::uint16_t>(value);
            std::memcpy(output, &half, 2); output += 2; size -= 2;
            if (!size) return;
        }
        value |= value << 16;
        while (size >= 4) {
            std::memcpy(output, &value, 4); output += 4; size -= 4;
        }
        if (size & 2) {
            const auto half = static_cast<std::uint16_t>(value);
            std::memcpy(output, &half, 2); output += 2;
        }
    }
    if (size & 1) *output = static_cast<std::uint8_t>(value);
}

// DS DMA transfers are synchronous CPU copies in this native backend. There
// is no second processor reading stale copies of these game allocations.
// Citro3D transfers use their own real GPU cache flush in GameRenderer;
// applying DS cache instructions to arbitrary 3DS addresses would be wrong.
// Keep ordering here without issuing privileged DS cache/MPU instructions.
void _ZN4CP1514FlushDataCacheEv() { __dmb(); }
void _ZN4CP1514FlushDataCacheEjj(std::uint32_t, std::uint32_t) { __dmb(); }
void _ZN4CP1516DrainWriteBufferEv() { __dmb(); }
void _ZN4CP1519InvalidateDataCacheEjj(std::uint32_t, std::uint32_t) { __dmb(); }
void _ZN4CP1526InvalidateInstructionCacheEjj(std::uint32_t, std::uint32_t) { __dmb(); }
void _ZN4CP1527FlushAndInvalidateDataCacheEv() { __dmb(); }
void _ZN4CP1527FlushAndInvalidateDataCacheEjj(std::uint32_t, std::uint32_t) { __dmb(); }
}  // extern "C"
