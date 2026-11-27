#pragma once
#include <atomic>
#include <cstdint>
namespace sm64ds::render::dialogue_window_view {
// Transitional PPU view: the native Message owner handles BG3 text and the
// rectangular BG0 tone. Other HUD objects remain visible in this local view.
// The original game's registers and resources are never changed here.
inline std::atomic<bool> owned{false};
inline std::atomic<bool> intro_map{false};
inline std::uint32_t map_display(std::uint32_t value) noexcept {
    return intro_map.load(std::memory_order_relaxed) ? value & ~0xf700u : value; // keep affine BG3
}
inline bool accepts_main_layers(unsigned layers) noexcept {return (layers&1u)&&!(layers&~0x19u);}
inline std::uint32_t dispcnt(std::uint32_t value,bool active) noexcept {
    return active?value&~std::uint32_t(0x0800|0xe000):value;
}
inline std::uint16_t control(unsigned offset,std::uint16_t value,bool active) noexcept {
    return active&&offset>=0x40&&offset<=0x54?0:value;
}
}
