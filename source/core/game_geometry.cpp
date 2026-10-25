#include "sm64ds/game_geometry.hpp"
#include <cstring>

namespace sm64ds {
namespace { GameGeometry geometry; }
GameGeometry& game_geometry() { return geometry; }
bool GameGeometry::set_texture_banks(uint16_t banks) {
    if(banks&~0x0fu || texture_transfer) return false;
    unsigned count=0;
    for(unsigned i=0;i<4;++i) count+=(banks>>i)&1u;
    texture_capacity=count*128*1024;
    ++texture_generation;return true;
}
bool GameGeometry::set_palette_banks(uint16_t banks) {
    if(banks&~0x70u || palette_transfer) return false;
    palette_capacity=((banks&0x10)?64*1024:0)+((banks&0x20)?16*1024:0)+
                     ((banks&0x40)?16*1024:0);
    ++palette_generation;return true;
}
bool GameGeometry::begin(bool palette) {
    bool& active=palette?palette_transfer:texture_transfer;
    if(active || !(palette?palette_capacity:texture_capacity)) return false;
    active=true;return true;
}
bool GameGeometry::upload(bool palette,const void* source,uint32_t offset,uint32_t size) {
    const uint32_t capacity=palette?palette_capacity:texture_capacity;
    if(!(palette?palette_transfer:texture_transfer) || (!source && size) ||
       offset>capacity || size>capacity-offset) return false;
    if(size) std::memcpy((palette?palettes:textures)+offset,source,size);
    if(palette) {++palette_uploads;palette_bytes+=size;}
    else {++texture_uploads;texture_bytes+=size;}
    return true;
}
bool GameGeometry::end(bool palette) {
    bool& active=palette?palette_transfer:texture_transfer;
    if(!active) return false;
    active=false;
    ++(palette?palette_generation:texture_generation);
    return true;
}
}
extern "C" void sm64ds_geometry_set_highlight(unsigned enabled) {
    sm64ds::game_geometry().highlight_shading=enabled!=0;
}
