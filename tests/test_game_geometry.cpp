#include "sm64ds/game_geometry.hpp"
#include <array>
#include <cassert>
#include <cstring>

int main() {
    static sm64ds::GameGeometry g;
    std::array<uint8_t,256> source{};
    for(unsigned i=0;i<source.size();++i) source[i]=uint8_t(i^0xa7);
    assert(!g.begin(false) && !g.begin(true));
    assert(!g.upload(false,source.data(),0,source.size()));
    // Original Stage config: A+B textures, E+F palettes.
    assert(g.set_texture_banks(3) && g.texture_capacity==256*1024);
    assert(g.set_palette_banks(0x30) && g.palette_capacity==80*1024);
    assert(!g.set_texture_banks(0x10) && g.texture_capacity==256*1024);
    assert(!g.set_palette_banks(3) && g.palette_capacity==80*1024);
    for(bool palette : {false,true}) {
        const auto capacity=palette?g.palette_capacity:g.texture_capacity;
        auto* buffer=palette?g.palettes:g.textures;
        assert(g.begin(palette) && !g.begin(palette));
        assert(!(palette?g.set_palette_banks(0x70):g.set_texture_banks(0xf)));
        assert(g.upload(palette,source.data(),capacity-256,256));
        assert(std::memcmp(buffer+capacity-256,source.data(),256)==0);
        assert(!g.upload(palette,source.data(),capacity-255,256));
        assert(!g.upload(palette,source.data(),0xffffffff,256));
        assert(!g.upload(palette,source.data(),1,0xffffffff));
        assert(!g.upload(palette,nullptr,0,1));
        assert(std::memcmp(buffer+capacity-256,source.data(),256)==0);
        assert(g.end(palette) && !g.end(palette));
        assert(!g.upload(palette,source.data(),0,1));
    }
    assert(g.texture_uploads==1 && g.palette_uploads==1);
    assert(g.texture_bytes==256 && g.palette_bytes==256);
    assert(g.texture_generation==2 && g.palette_generation==2);
    // Format-5 compressed texels and index data occupy two separate ranges.
    assert(g.begin(false));
    assert(g.upload(false,source.data(),0,256));
    assert(g.upload(false,source.data()+128,0x20000,128));
    assert(g.end(false));
    assert(std::memcmp(g.textures,source.data(),256)==0);
    assert(std::memcmp(g.textures+0x20000,source.data()+128,128)==0);
    sm64ds_geometry_set_highlight(1);assert(sm64ds::game_geometry().highlight_shading);
    sm64ds_geometry_set_highlight(0);assert(!sm64ds::game_geometry().highlight_shading);
}
