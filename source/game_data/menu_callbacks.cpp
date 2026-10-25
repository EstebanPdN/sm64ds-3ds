#include "sm64ds/game_data.hpp"
#include "rom_profile.hpp"
#include <cstring>

extern "C" {
alignas(4) uint32_t data_ov007_02103290[6];
alignas(4) int32_t data_ov007_02103260;
alignas(4) uint32_t data_ov007_02103254;
alignas(4) uint32_t data_ov007_02103258;
}

namespace sm64ds::game_data {
bool initialize_menu_callbacks(AssetPack& pack, const NativeResolver& resolver,
                               std::string& error) {
    const auto* profile=rom_profile(pack.info());
    if(!profile) {
        error="Callbacks del menu: perfil de ROM no verificado";return false;
    }
    OverlayImage menu;
    if(!load_overlay(pack,7,menu,error)) return false;
    size_t off,state,rtti_name,rtti_ptr,defaults;
    if(!menu.range(profile->callbacks,24,off) || crc32(menu.bytes.data()+off,24)!=profile->callbacks_crc ||
       !menu.range(profile->state,4,state) || crc32(menu.bytes.data()+state,4)!=0xffffffffu ||
       !menu.range(profile->defaults,8,defaults) || crc32(menu.bytes.data()+defaults,8)!=profile->defaults_crc ||
       !menu.range(profile->rtti_name,11,rtti_name) || std::memcmp(menu.bytes.data()+rtti_name,"9dScDSMT_c",11) ||
       !menu.range(profile->rtti_reference,4,rtti_ptr)) {
        error="Callbacks del menu: el grupo DSMT no coincide con el perfil";return false;
    }
    // The record is independently anchored by DSMT's RTTI string/reference and
    // the title's scalar whose address came from a whole-function match.
    const uint8_t* ptr=menu.bytes.data()+rtti_ptr;
    const uint32_t name_pointer=uint32_t(ptr[0])|uint32_t(ptr[1])<<8|
                                uint32_t(ptr[2])<<16|uint32_t(ptr[3])<<24;
    if(name_pointer!=profile->rtti_name) {error="Referencia RTTI DSMT incorrecta";return false;}
    const auto& funcs=profile->callback_functions;
    const std::vector<Relocation> relocations={
        {0, funcs[0],"ov007","func_ov007_020ccab4",0,SymbolKind::function},
        {4, funcs[1],"ov007","func_ov007_020cca98",0,SymbolKind::function},
        {8, funcs[2],"ov007","func_ov007_020cca80",0,SymbolKind::function},
        {12,funcs[3],"ov007","func_ov007_020cca74",0,SymbolKind::function},
        {16,funcs[4],"ov007","func_ov007_020cca68",0,SymbolKind::function},
        {20,funcs[5],"ov007","func_ov007_020cc600",0,SymbolKind::function},
        {24,funcs[6],"ov007","func_ov007_020c3e4c",0,SymbolKind::function},
        {28,funcs[7],"ov007","func_ov007_020c3e64",0,SymbolKind::function},
    };
    std::vector<uint8_t> source(menu.bytes.begin()+off,menu.bytes.begin()+off+24),relocated;
    source.insert(source.end(),menu.bytes.begin()+defaults,menu.bytes.begin()+defaults+8);
    if(!relocate_block(source,relocations,resolver,relocated,error)) return false;
    uint32_t words[8];
    for(unsigned i=0;i<8;++i) {
        const auto* p=relocated.data()+i*4;
        words[i]=uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;
    }
    const auto* p=menu.bytes.data()+state;
    const uint32_t initial=uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;
    // No globals are changed on an unresolved callback or an invalid ROM.
    std::memcpy(data_ov007_02103290,words,sizeof data_ov007_02103290);
    data_ov007_02103260=int32_t(initial);
    data_ov007_02103254=words[6];
    data_ov007_02103258=words[7];
    return true;
}
} // namespace sm64ds::game_data
