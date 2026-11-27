#pragma once
#include "contracts.hpp"
#include <cstring>

namespace sm64ds::render::game {
// Semantic water/cloth grid used by the file selector. Positions and normals
// belong to the simulation. This conversion retains its staggered strip
// topology and fixed-point UV perturbation; PICA transforms/lights the result.
struct WaveGrid {
    const void* positions=nullptr; // packed signed 20.12 xyz
    const void* normals=nullptr;   // packed signed 4.12 xyz
    unsigned columns=0,rows=0,image_width=0,image_height=0;
    std::int32_t u=0,v=0,du=0,dv=0,normal_u=0,normal_v=0;
    std::uint8_t alpha=31;
};
inline Error wave_grid(const WaveGrid& g,NormalVertex* out,std::size_t capacity,std::size_t& written) noexcept {
    if(!g.positions||!g.normals||!out||!g.columns||!g.rows||g.columns>255||g.rows>255||
       !g.image_width||!g.image_height||g.alpha>31)return Error::invalid_data;
    const auto count=std::size_t(g.columns)*g.rows*6;
    if(count>capacity)return Error::capacity;
    auto signed16=[](std::int64_t n){const auto bits=std::uint16_t(n);return bits>=32768?int(bits)-65536:int(bits);};
    auto signed10=[](int n){const unsigned bits=unsigned(n)&1023;return bits>=512?int(bits)-1024:int(bits);};
    auto floor_shift=[](std::int64_t n,unsigned s){return n>=0?n/(std::int64_t(1)<<s):-((-n+(std::int64_t(1)<<s)-1)/(std::int64_t(1)<<s));};
    auto vertex=[&](unsigned row,unsigned column,std::int32_t stagger){
        const auto index=std::size_t(row)*(g.columns+1)+column;
        std::int32_t p[3];std::int16_t n[3];
        std::memcpy(p,static_cast<const char*>(g.positions)+index*12,12);
        std::memcpy(n,static_cast<const char*>(g.normals)+index*6,6);
        NormalVertex result{};
        result.base.x=float(signed16(floor_shift(p[0],6)))/4096;
        result.base.y=float(signed16(floor_shift(p[1],6)))/4096;
        result.base.z=float(signed16(floor_shift(p[2],6)))/4096;
        const auto u=std::int64_t(g.u)+stagger+std::int64_t(g.du)*column+floor_shift(std::int64_t(n[0])*g.normal_u+2048,12);
        const auto v=std::int64_t(g.v)+std::int64_t(g.dv)*row+floor_shift(std::int64_t(n[1])*g.normal_v+2048,12);
        result.base.u=float(signed16(floor_shift(u,8)))/(16*g.image_width);
        result.base.v=float(signed16(floor_shift(v,8)))/(16*g.image_height);
        result.base.r=result.base.g=result.base.b=255;result.base.a=std::uint8_t((g.alpha*255+15)/31);
        result.x=std::int16_t(signed10(int(floor_shift(n[0],3))));
        result.y=std::int16_t(signed10(int(floor_shift(n[1],3))));
        result.z=std::int16_t(signed10(int(floor_shift(n[2],3))));result.uv_source=2;
        return result;
    };
    // BUILD137: a grid point's stagger depends only on its own row parity
    // (row r uses du/2 when r is odd, both as the top and as the bottom of a
    // strip), so each point is converted once into a row cache instead of up
    // to six times. Same vertex() per point, so the output is bit-identical.
    // The file selector draws this grid twice per update on the game thread
    // (4.3 ms per update in Azahar New 3DS at 50% clock, BUILD 137 probe).
    // Game thread only: the two row caches are static.
    static NormalVertex rows_cache[2][256];
    auto convert_row=[&](unsigned row,NormalVertex* into){
        const std::int32_t stagger=(row&1)?g.du/2:0;
        for(unsigned column=0;column<=g.columns;++column)into[column]=vertex(row,column,stagger);
    };
    NormalVertex* top=rows_cache[0];NormalVertex* bottom=rows_cache[1];
    convert_row(0,top);
    std::size_t at=0;
    for(unsigned row=0;row<g.rows;++row){
        convert_row(row+1,bottom);
        for(unsigned j=0;j<g.columns;++j){
            const unsigned a=(row&1)?g.columns-j:j,b=(row&1)?a-1:a+1;
            const auto& t0=top[a];const auto& t1=top[b];const auto& b0=bottom[a];const auto& b1=bottom[b];
            out[at++]=t0;out[at++]=b0;out[at++]=t1;
            out[at++]=t1;out[at++]=b0;out[at++]=b1;
        }
        auto* next=top;top=bottom;bottom=next;
    }
    written=count;return Error::none;
}
}
