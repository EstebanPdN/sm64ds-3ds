#include "sm64ds/game_data.hpp"
#include "rom_profile.hpp"
#include <cstring>

// Storage only: bytes are supplied from the user's verified cartridge at boot.
// Neither symbol contains a function/data pointer or needs an ABI relocation.
extern "C" {
alignas(4) int16_t data_02082214[8192];
alignas(4) int32_t data_ov007_0210325c;
}

namespace sm64ds::game_data {
bool initialize_native_constants(AssetPack& pack, std::string& error) {
    const auto* profile=rom_profile(pack.info());
    if(!profile) {
        error="Constantes nativas: esta revision de ROM aun no tiene un perfil verificado";
        return false;
    }
    OverlayImage arm9,menu;
    if(!load_arm9(pack,arm9,error) || !load_overlay(pack,7,menu,error)) return false;
    size_t trig,scale;
    if(!arm9.range(profile->trig,16384,trig) ||
       crc32(arm9.bytes.data()+trig,16384)!=0xee067d57u ||
       !menu.range(profile->scale,4,scale) ||
       crc32(menu.bytes.data()+scale,4)!=0x3d627c6cu) {
        error="Constantes nativas: las tablas no coinciden con el perfil de ROM";
        return false;
    }
    // Publication is after every check; a rejected input changes neither global.
    for(size_t i=0;i<8192;++i) {
        const auto* p=arm9.bytes.data()+trig+i*2;
        data_02082214[i]=int16_t(uint16_t(p[0])|uint16_t(p[1])<<8);
    }
    const auto* p=menu.bytes.data()+scale;
    data_ov007_0210325c=int32_t(uint32_t(p[0])|uint32_t(p[1])<<8|
                               uint32_t(p[2])<<16|uint32_t(p[3])<<24);
    return true;
}
} // namespace sm64ds::game_data
