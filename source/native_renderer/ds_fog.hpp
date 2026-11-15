#pragma once
#include "contracts.hpp"
#include <array>
#include <cmath>
#include <cstring>

namespace sm64ds::render {
// The DS fog registers as the game left them for an image: DISP3DCNT bit 7
// (enable), bit 6 (alpha only) and bits 8-11 (shift), FOG_OFFSET, FOG_COLOR
// and the 32 densities of FOG_TABLE.
struct DsFog {
    bool enabled=false,alpha_only=false;
    std::uint8_t shift=0;
    std::uint16_t offset=0;   // 15 bits of depth
    std::uint32_t colour=0;   // BGR555, five bits of alpha above
    std::array<std::uint8_t,32> table{};
};
inline bool same(const DsFog& a,const DsFog& b) noexcept {
    return a.enabled==b.enabled&&a.alpha_only==b.alpha_only&&a.shift==b.shift&&a.offset==b.offset&&a.colour==b.colour&&a.table==b.table;
}
// Density (0..128 of 128) the DS applies to a pixel whose depth buffer holds
// `depth24`. The depth minus the offset is shifted right by two and left by
// the fog shift in 32 bits (large shifts wrap, as on hardware); bits 17 up
// index the table, the rest interpolate between neighbours; 127 counts as
// full. Entry 0 is repeated below the first boundary and entry 31 above the
// last one.
inline unsigned ds_fog_density(const DsFog& fog,std::uint32_t depth24) noexcept {
    const std::uint32_t start=std::uint32_t(fog.offset&0x7fffu)*0x200u;
    unsigned index=0;std::uint32_t fraction=0;
    if(depth24>=start){
        const std::uint32_t z=((depth24-start)>>2)<<(fog.shift&15u);
        index=z>>17;
        if(index>=32){index=32;fraction=0;}else fraction=z&0x1ffffu;
    }
    const auto entry=[&fog](unsigned i){return std::uint32_t(fog.table[i==0?0:i>32?31:i-1]&127u);};
    const unsigned density=unsigned((entry(index)*(0x20000u-fraction)+entry(index+1)*fraction)>>17);
    return density>=127?128:density;
}
// SM64DS draws with W-buffering (SWAP_BUFFERS=3): the depth the fog reads is
// the clip-space w in 20.12, up to 24 bits. The native target keeps a window
// depth p = -(clip z)/w in [0,1] (1 nearest), and clip z = a*w + b for a
// perspective projection, so w = -b/(p+a).
struct FogDepth {float a=0,b=0;bool perspective=false;};
inline FogDepth fog_depth(const Matrix& view_to_clip) noexcept {
    FogDepth depth;const float wz=view_to_clip.row[3][2];
    if(!(std::fabs(wz)>1e-12f))return depth; // w does not follow the distance
    depth.a=view_to_clip.row[2][2]/wz;depth.b=view_to_clip.row[2][3]-depth.a*view_to_clip.row[3][3];
    depth.perspective=std::isfinite(depth.a)&&std::isfinite(depth.b)&&depth.b!=0;
    return depth;
}
inline std::uint32_t fog_depth24(FogDepth depth,float p) noexcept {
    const float denominator=p+depth.a;
    if(denominator==0)return 0xffffffu;
    const float w=-depth.b/denominator;
    if(!(w>=0))return 0xffffffu;        // behind the far end of the mapping
    return w>=4095.999f?0xffffffu:std::uint32_t(w*4096.f);
}
inline float ds_fog_factor(const DsFog& fog,FogDepth depth,float p) noexcept {
    return 1.f-float(ds_fog_density(fog,fog_depth24(depth,p)))/128.f;
}
// PICA200 fog table: 128 intervals of window depth, each with its own start
// value and change (1 = untouched pixel, 0 = fog colour). The intervals need
// not join, so each is the least-squares line of the DS density over its own
// span. APPROXIMATION: the PICA indexes its table by window depth, the DS by
// distance; almost all of the DS ramp falls in the last few far intervals,
// where one line stands for a curve. max_error (optional) receives the
// largest difference found, in fog factor.
inline void build_fog_lut(const DsFog& fog,FogDepth depth,float* data,float* max_error=nullptr) noexcept {
    constexpr unsigned samples=32;float worst=0;
    for(unsigned i=0;i<128;++i){
        float sx=0,sy=0,sxx=0,sxy=0;float value[samples+1];
        for(unsigned s=0;s<=samples;++s){
            const float x=float(s)/samples,y=ds_fog_factor(fog,depth,(float(i)+x)/128.f);
            value[s]=y;sx+=x;sy+=y;sxx+=x*x;sxy+=x*y;
        }
        const float n=samples+1,slope=(n*sxy-sx*sy)/(n*sxx-sx*sx);
        const auto clamp=[](float v){return v<0?0.f:v>1?1.f:v;};
        const float first=clamp((sy-slope*sx)/n),last=clamp((sy-slope*sx)/n+slope);
        data[i]=first;data[128+i]=last-first;
        for(unsigned s=0;s<=samples;++s){const float e=std::fabs(first+(last-first)*float(s)/samples-value[s]);if(e>worst)worst=e;}
    }
    if(max_error)*max_error=worst;
}
// FOG_COLOR's BGR555 as the PICA's fog colour word (red in the low byte).
inline std::uint32_t fog_colour(const DsFog& fog) noexcept {
    const auto channel=[&fog](unsigned shift){const std::uint32_t v=(fog.colour>>shift)&31u;return (v<<3)|(v>>2);};
    return channel(0)|(channel(5)<<8)|(channel(10)<<16);
}
// What the native target can show: colour fog over a perspective image.
inline bool fog_drawable(const DsFog& fog,FogDepth depth) noexcept {return fog.enabled&&!fog.alpha_only&&depth.perspective;}
}
