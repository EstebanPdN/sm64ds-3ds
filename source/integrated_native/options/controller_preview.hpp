#pragma once
#include <cstdint>
namespace enhance::controller_options {
// Original PS_Update case7 selects one of three already-loaded upper BG2 maps.
// Preserve the same priority/character flags; do not rewrite shared glyph VRAM.
constexpr uint16_t preview_bg2cnt(uint16_t previous,unsigned mode){
 return uint16_t((((mode%3)+0xdu)<<8)|(previous&0x43u)|0x10u);
}
}
