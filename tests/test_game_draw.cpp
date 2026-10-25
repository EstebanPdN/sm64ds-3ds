#include "sm64ds/game_draw.hpp"
#include "sm64ds/game_textures.hpp"
#include "ntr/gx.h"
#include <cassert>
#include <cmath>
#include <cstring>
static void port(unsigned p,uint32_t v) {ntr::gx_write_port(0x04000400+p*4,v);}
static void triangle() {
    port(0x40,0);
    for(unsigned i=0;i<3;++i)port(0x24,(i==1?64:0)|(i==2?64u<<10:0));
    port(0x41,0);
}
int main() {
    // Ordered ports/FIFO bytes, including unaligned source lists. A rejected
    // list never appends a partial packet or reports a successful frame.
    sm64ds_draw_reset();sm64ds_draw_port(0x10,2);sm64ds_draw_fifo(0x17);
    uint8_t source[]={0xa5,1,2,3,4,5,6,7,8};sm64ds_draw_list(source+1,8);
    assert(sm64ds_draw_ok() && sm64ds_draw_count()==4);
    assert(sm64ds_draw_entries()[2].value==0x04030201);
    sm64ds_draw_list(source,3);assert(!sm64ds_draw_ok() && sm64ds_draw_count()==4);
    sm64ds_draw_reset();sm64ds_draw_list(nullptr,4);assert(!sm64ds_draw_ok());
    sm64ds_draw_reset();sm64ds_draw_port(0x100,1);assert(!sm64ds_draw_ok());
    sm64ds_draw_reset();
    for(unsigned i=0;i<sm64ds::draw_capacity;++i)sm64ds_draw_fifo(i);
    assert(sm64ds_draw_ok());sm64ds_draw_fifo(0);assert(!sm64ds_draw_ok());
    assert(sm64ds_draw_count()==sm64ds::draw_capacity);

    auto& g=sm64ds::game_geometry();
    assert(g.set_texture_banks(15) && g.set_palette_banks(0x70));
    uint8_t texels[32];std::memset(texels,0x11,sizeof texels);
    uint16_t palette[16]={0,0x001f};
    assert(g.begin(false) && g.upload(false,texels,0,sizeof texels) && g.end(false));
    assert(g.begin(true) && g.upload(true,palette,0,sizeof palette) && g.end(true));
    sm64ds::GameTextures textures(g);
    const uint32_t* old=nullptr;int w=0,h=0;
    assert(textures.resolve(3u<<26,0,old,w,h) && w==8 && h==8 && old[0]==0xffff0000);
    // A palette upload away from the first word must create a new decoded
    // version without invalidating the old triangles in this same frame.
    const uint16_t green=0x03e0;
    assert(g.begin(true) && g.upload(true,&green,2,2) && g.end(true));
    const uint32_t* fresh=nullptr;
    assert(textures.resolve(3u<<26,0,fresh,w,h) && fresh[0]==0xff00ff00 && old[0]==0xffff0000);
    assert(textures.cached_textures()==2);textures.begin_frame();assert(textures.cached_textures()==1);
    assert(!textures.resolve((7u<<26)|0xffffu|(7u<<20)|(7u<<23),0,fresh,w,h));
    assert(!textures.resolve(3u<<26,0x2000,fresh,w,h));

    // Format-5 index words for slot 2 use the UPPER half of slot 1.
    std::memset(g.textures+0x40000,0,16);
    for(unsigned i=0;i<8;i+=2) {g.textures[0x30000+i]=0;g.textures[0x30000+i+1]=0x80;}
    assert(textures.resolve((5u<<26)|0x8000u,0,fresh,w,h) && fresh[0]==0xff000000);
    assert(!textures.resolve((5u<<26)|0x4000u,0,fresh,w,h)); // slot 1 is not a texel slot

    ntr::gx_reset();ntr::gx_set_texture_resolver(sm64ds::GameTextures::resolver,&textures);
    port(0x2b,0);port(0x2a,3u<<26);triangle();
    std::size_t count=0;auto* triangles=ntr::gx_polygons(count);
    assert(count==1 && ntr::gx_asset_stream_valid() && triangles[0].tex[0]==0xff00ff00);
    // Texture mode 1 must retain the texture matrix, including its offset.
    ntr::gx_reset();port(0x10,3);
    const int32_t matrix[]={8192,0,0,0,12288,0,0,0,4096,65536,131072,0};
    for(auto value:matrix)port(0x17,uint32_t(value));
    port(0x10,1);port(0x2a,(3u<<26)|(1u<<30));port(0x22,16|(32u<<16));triangle();
    triangles=ntr::gx_polygons(count);
    assert(count==1 && ntr::gx_asset_stream_valid());
    assert(triangles[0].v[0].u==3 && triangles[0].v[0].v==8);
    ntr::gx_set_texture_resolver(nullptr,nullptr);
    // The game's LIGHT_* ports describe lights, while POLYGON_ATTR bits 0..3
    // select which lights NORMAL actually uses. Test the emitted vertex colour,
    // not an internal mask getter: changing only alpha would keep every case
    // black. A white diffuse material lit by red/green directional lights gives
    // black/red/green/yellow for the four masks below.
    const uint32_t lit_colors[]={0xff000000u,0xffff0000u,0xff00ff00u,0xffffff00u};
    for(unsigned mask=0;mask<4;++mask) {
        ntr::gx_reset();
        port(0x32,0x200u<<20);           // light 0 direction (0,0,-1), signed 1.9
        port(0x33,0x001f);              // light 0 red
        port(0x32,(1u<<30)|(0x200u<<20));
        port(0x33,(1u<<30)|0x03e0);     // light 1 green, same direction
        port(0x30,0x7fff|0x8000);       // white diffuse, zero ambient
        port(0x31,0);                   // zero emission
        port(0x29,(31u<<16)|0xc0u|mask);
        port(0x21,0x1ffu<<20);          // front normal (0,0,+511/512)
        triangle();
        triangles=ntr::gx_polygons(count);
        assert(ntr::gx_asset_stream_valid() && count==1);
        for(const auto& vertex:triangles[0].v)assert(vertex.color==lit_colors[mask]);
    }
    ntr::gx_reset();port(0x2a,3u<<26);triangle();assert(!ntr::gx_asset_stream_valid());
    auto diagnostic=ntr::gx_get_stream_diagnostics();
    assert(diagnostic.failure==ntr::GxStreamFailure::texture_resolution &&
           diagnostic.texture_image==3u<<26 && diagnostic.texture_palette==0);
    // A later material must not erase the first offending binding.
    port(0x2a,2u<<26);port(0x2b,37);
    diagnostic=ntr::gx_get_stream_diagnostics();
    assert(diagnostic.texture_image==3u<<26 && diagnostic.texture_palette==0);
    ntr::gx_reset();ntr::gx_write_fifo(0x17);assert(!ntr::gx_asset_stream_valid());
    diagnostic=ntr::gx_get_stream_diagnostics();
    assert(diagnostic.failure==ntr::GxStreamFailure::none && diagnostic.fifo_command==0x17 &&
           diagnostic.fifo_parameters==0 && diagnostic.fifo_needed==12 && diagnostic.fifo_commands_remaining==4);
    ntr::gx_write_fifo(4096);
    assert(ntr::gx_get_stream_diagnostics().fifo_parameters==1);
    ntr::gx_reset();port(0x1b,4096);
    diagnostic=ntr::gx_get_stream_diagnostics();
    assert(diagnostic.failure==ntr::GxStreamFailure::none && diagnostic.port_command==0x1b &&
           diagnostic.port_parameters==1 && diagnostic.port_needed==3);
    ntr::gx_reset();port(0x61,0);assert(!ntr::gx_asset_stream_valid());
    diagnostic=ntr::gx_get_stream_diagnostics();
    assert(diagnostic.failure==ntr::GxStreamFailure::unknown_command && diagnostic.failing_command==0x61);
    ntr::gx_reset();port(0x2a,(3u<<30)|(3u<<26));
    assert(ntr::gx_get_stream_diagnostics().failure==ntr::GxStreamFailure::texture_generation);
    ntr::gx_reset();assert(ntr::gx_asset_stream_valid() &&
                          ntr::gx_get_stream_diagnostics().failure==ntr::GxStreamFailure::none);
}
