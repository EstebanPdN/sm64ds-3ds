#pragma once
#include "libsm64.h"
#include "decomp/include/surface_terrains.h"
#include <cstdint>
namespace movement_geometry {
enum class MaterialUse:uint8_t {Solid,Water,NeedsDsEffect,UnknownFriction};
struct MaterialProfile {
    uint32_t raw[2]{};
    uint8_t floor_class=0,surface_effect=0,ground_sound=0,parameter=0;
    uint8_t bits6_11=0,bits15_18=0,filter_flags=0;
    bool class1_latch=false;
    int16_t n64_type=-1;
    uint16_t n64_terrain=TERRAIN_GRASS;
    MaterialUse use=MaterialUse::UnknownFriction;
};
inline MaterialProfile material_profile(uint32_t first,uint32_t second) {
    MaterialProfile p;p.raw[0]=first;p.raw[1]=second;
    p.floor_class=uint8_t((first>>12)&7);p.surface_effect=uint8_t((first>>19)&31);
    p.ground_sound=uint8_t(first&31);p.parameter=uint8_t(second&255);
    p.bits6_11=uint8_t((first>>6)&63);p.bits15_18=uint8_t((first>>15)&15);
    p.filter_flags=uint8_t((first>>24)&7);p.class1_latch=p.floor_class==1;
    switch(p.floor_class) {
        case 0:p.n64_type=SURFACE_DEFAULT;break;
        case 1:case 2:p.n64_type=SURFACE_NOT_SLIPPERY;break;
        case 3:p.n64_type=SURFACE_SLIPPERY;break;
        case 4:p.n64_type=SURFACE_VERY_SLIPPERY;break;
        // DS promoted N64's slide-terrain special case to its own class.
        case 5:p.n64_type=SURFACE_VERY_SLIPPERY;p.n64_terrain=TERRAIN_SLIDE;break;
    }
    if(first&0x20)p.use=MaterialUse::Water;
    else if(p.floor_class>=6)p.use=MaterialUse::UnknownFriction;
    else if(p.surface_effect || (first&0x02000000))p.use=MaterialUse::NeedsDsEffect;
    else p.use=MaterialUse::Solid;
    return p;
}
// Only ordinary locomotion properties are mapped here. Quicksand/death/warp/
// other DS effects still require the native gameplay owner; never flatten them
// to default ground. Raw CLPS, sound/parameter fields and class1 latch survive.
inline bool apply_locomotion_material(const MaterialProfile& p,SM64SurfaceCollisionData& surface) {
    if(p.use!=MaterialUse::Solid)return false;
    surface.type=p.n64_type;surface.terrain=p.n64_terrain;surface.force=0;return true;
}
// Ceiling queries run every tick, including ceilings thousands of units away.
// Unsupported effects must keep their DS owner when reachable, but must not
// veto ordinary movement merely because a distant ceiling has that property.
// 160 is the N64 full body collision height; callers may already query above
// the feet, so this is deliberately conservative. Never changes floor rules.
inline bool distant_effect_ceiling(const MaterialProfile& p,int32_t height_fix12,int32_t query_y){
    return p.use!=MaterialUse::Solid && int64_t(height_fix12)>(int64_t(query_y)+160)*4096;
}
}
