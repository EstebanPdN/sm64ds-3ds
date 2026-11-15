#include "affine_image.hpp"
#include "screen_scene.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
using namespace sm64ds::render;

int main(){
    std::uint32_t pixel=0;
    AffineImage image{&pixel,&pixel,1,512,256};assert(valid(image));
    image.height=192;assert(!valid(image));image.height=256;
    image.width=1024;assert(!valid(image));image.width=512;
    // Independent integer texel oracle. Include negative references, rotations,
    // shear, exact texel boundaries and both output edges. The CPU packs only
    // two affine rows; no per-frame pixel/vertex transformation is required.
    std::uint32_t rng=0x5d3ca021u;
    auto next=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
    auto floor256=[](std::int64_t v){return v>=0?v/256:-((-v+255)/256);};
    for(unsigned n=0;n<10000;++n){
        image.x=int(next()%200001)-100000;image.y=int(next()%200001)-100000;
        image.xx=std::int16_t(int(next()%1025)-512);image.xy=std::int16_t(int(next()%1025)-512);
        image.yx=std::int16_t(int(next()%1025)-512);image.yy=std::int16_t(int(next()%1025)-512);
        const auto uv=affine_image_uv(image);
        for(unsigned k=0;k<16;++k){
            const unsigned x=k<4?(k&1?255:0):next()%256,y=k<4?(k&2?191:0):next()%192;
            const double sx=(x+.5)/256,sy=(y+.5)/192;
            const auto u=std::floor((uv[0]*sx+uv[1]*sy+uv[2])*image.width);
            const auto v=std::floor((uv[3]*sx+uv[4]*sy+uv[5])*image.height);
            assert(u==floor256(std::int64_t(image.x)+image.xx*int(x)+image.xy*int(y)));
            assert(v==floor256(std::int64_t(image.y)+image.yx*int(x)+image.yy*int(y)));
        }
    }
    auto material=screen_material();
    material.wrap_s=Wrap::transparent_border;assert(valid(material));
    material.wrap_s=static_cast<Wrap>(3);assert(!valid(material));
    material.wrap_s=static_cast<Wrap>(5);assert(!valid(material));
}
