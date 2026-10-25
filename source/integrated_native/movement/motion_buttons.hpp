#pragma once
#include <cstdint>
namespace native_motion {
// Stage::CheckInput applies the actual DS Standard/Touch/Dual-hand table.
// Share its logical actions; physics selection must not impose another layout.
constexpr uint32_t Jump=1,Attack=2,Crouch=4;
constexpr uint32_t motion_buttons(uint16_t logical){return ((logical&2)?Jump:0)|((logical&1)?Attack:0)|((logical&0x400)?Crouch:0);}
constexpr uint32_t filter_circle_digital(uint32_t held,bool analog,bool gameplay,bool paused){return analog&&gameplay&&!paused?held&~uint32_t(0xf0000000u):held;}
}
