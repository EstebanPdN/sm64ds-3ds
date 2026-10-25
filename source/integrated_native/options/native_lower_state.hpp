#pragma once
#include <atomic>
#include <cstdint>
namespace native_lower {
// Game thread publishes a native UI image; worker confirms texture ownership.
// Filter only the PPU's local DISPCNT read, never the real game registers/VRAM.
inline std::atomic<bool> wanted{false},gpu_ready{false};
inline uint32_t lower_dispcnt(uint32_t value,bool replace){
    return replace?value&~uint32_t((1u<<8)|(1u<<9)|(1u<<12)):value;
}
inline bool replacing(){return wanted.load(std::memory_order_acquire)&&gpu_ready.load(std::memory_order_acquire);}
}
