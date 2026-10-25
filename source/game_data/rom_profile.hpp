#pragma once
#include "sm64ds/assets.hpp"
#include <array>

namespace sm64ds::game_data {
// Addresses and checksums only. The actual data always comes from the ROM.
struct RomProfile {
    const char* code;
    uint32_t crc, trig, scale, callbacks, callbacks_crc, state;
    uint32_t defaults, defaults_crc, rtti_name, rtti_reference;
    std::array<uint32_t,8> callback_functions;
};
inline const RomProfile* rom_profile(const RomInfo& rom) {
    static constexpr RomProfile profiles[] = {
        {"ASME",0xe6321562,0x0207fee0,0x020f99bc,0x020f99f0,0xa0784624,
         0x020f99c0,0x020f99b4,0x2229e9ba,0x020f99cc,0x020f99dc,
         {0x020c3418,0x020c33fc,0x020c33e4,0x020c33d8,0x020c33cc,0x020c2f64,0x020ba7ac,0x020ba7c4}},
        {"ASMP",0x29715dec,0x02082214,0x0210325c,0x02103290,0x84eec1fd,
         0x02103260,0x02103254,0x3644c331,0x0210326c,0x0210327c,
         {0x020ccab4,0x020cca98,0x020cca80,0x020cca74,0x020cca68,0x020cc600,0x020c3e4c,0x020c3e64}},
    };
    if(rom.revision!=0) return nullptr;
    for(const auto& p:profiles) if(rom.code==p.code && rom.crc==p.crc) return &p;
    return nullptr;
}
}
