#include "game_wave_grid.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <vector>
using namespace sm64ds::render;
// BUILD137 reference: the pre-BUILD137 conversion (every strip vertex
// converted on its own). The row-cached version must match it byte for byte.
namespace {
std::vector<NormalVertex> reference(const game::WaveGrid& g){
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
    std::vector<NormalVertex> out;
    for(unsigned row=0;row<g.rows;++row)for(unsigned j=0;j<g.columns;++j){
        const unsigned a=(row&1)?g.columns-j:j,b=(row&1)?a-1:a+1;
        const auto t0=vertex(row,a,(row&1)?g.du/2:0),t1=vertex(row,b,(row&1)?g.du/2:0);
        const auto b0=vertex(row+1,a,(row&1)?0:g.du/2),b1=vertex(row+1,b,(row&1)?0:g.du/2);
        out.push_back(t0);out.push_back(b0);out.push_back(t1);out.push_back(t1);out.push_back(b0);out.push_back(b1);
    }
    return out;
}
std::uint32_t state=0x12345678u;
std::uint32_t next(){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
}
int main(){
    // Two rows reverse strip traversal and diagonal. Each row uses a half
    // texel-step stagger on its opposite side, including shared vertices.
    std::int32_t p[9][3]{};std::int16_t n[9][3]{};
    for(unsigned y=0;y<3;++y)for(unsigned x=0;x<3;++x){p[y*3+x][0]=int(x)*262144;p[y*3+x][1]=int(y)*262144;n[y*3+x][2]=-4096;}
    game::WaveGrid grid;grid.positions=p;grid.normals=n;grid.columns=grid.rows=2;grid.image_width=grid.image_height=8;grid.du=grid.dv=8192;
    std::array<NormalVertex,24> out{};std::size_t count=99;
    assert(game::wave_grid(grid,out.data(),23,count)==Error::capacity&&count==99&&out[0].base.a==0);
    assert(game::wave_grid(grid,out.data(),out.size(),count)==Error::none&&count==24);
    assert(out[0].base.x==0&&out[0].base.y==0&&out[0].base.u==0);
    assert(out[1].base.x==0&&out[1].base.y==1&&out[1].base.u==.125f&&out[1].base.v==.25f);
    assert(out[2].base.x==1&&out[2].base.y==0&&out[2].base.u==.25f);
    assert(out[12].base.x==2&&out[12].base.y==1&&out[12].base.u==.625f);
    assert(out[13].base.x==2&&out[13].base.y==2&&out[13].base.u==.5f);
    assert(out[14].base.x==1&&out[14].base.y==1&&out[14].base.u==.375f);
    assert(out[0].z==-512&&out[0].uv_source==2&&out[0].base.a==255);
    // Negative fixed-point coordinates floor before signed16 narrowing; UVs
    // wrap BEFORE interpolation, rather than after GPU sampling.
    p[0][0]=-1;grid.u=8388608;grid.alpha=16;
    assert(game::wave_grid(grid,out.data(),out.size(),count)==Error::none);
    assert(out[0].base.x==-1.f/4096&&out[0].base.u==-256&&out[0].base.a==132);
    grid.u=0;grid.normal_u=4096;n[0][0]=-2049;
    assert(game::wave_grid(grid,out.data(),out.size(),count)==Error::none);
    assert(out[0].base.u==-9.f/128&&out[0].x==-257);
    // BUILD137: row-cached conversion == reference, byte for byte, over
    // random grids (odd/even row counts, odd du, negative values, max 255 columns).
    for(unsigned trial=0;trial<200;++trial){
        game::WaveGrid g;g.columns=1+next()%(trial==0?255u:20u);g.rows=1+next()%(trial==1?255u:20u);
        std::vector<std::int32_t> pos(std::size_t(g.columns+1)*(g.rows+1)*3);std::vector<std::int16_t> nor(pos.size());
        for(auto& v:pos)v=std::int32_t(next());for(auto& v:nor)v=std::int16_t(next());
        g.positions=pos.data();g.normals=nor.data();g.image_width=8u<<(next()%6);g.image_height=8u<<(next()%6);
        g.u=std::int32_t(next());g.v=std::int32_t(next());g.du=std::int32_t(next()%65536)-32768;g.dv=std::int32_t(next()%65536)-32768;
        g.normal_u=std::int32_t(next()%16384)-8192;g.normal_v=std::int32_t(next()%16384)-8192;g.alpha=std::uint8_t(next()%32);
        const auto expected=reference(g);std::vector<NormalVertex> got(expected.size()+1);std::size_t written=0;
        assert(game::wave_grid(g,got.data(),got.size(),written)==Error::none&&written==expected.size());
        assert(std::memcmp(got.data(),expected.data(),expected.size()*sizeof(NormalVertex))==0);
    }
}
