#include "../../source/integrated_native/renderer/bridge_composition.hpp"
#include <cassert>
#include <array>

using sm64ds::render::bridge_policy::Pixel;
using sm64ds::render::bridge_policy::foreground;
int main(){
    Pixel p;p.front=0xfff0e0d0;p.resolved=0xff102030;
    // A dialogue's brightness applies inside its effect window, never outside.
    p.blend=0xc1;p.brightness=8;
    assert(foreground(p)==0x80000000u);
    p.window=31;assert(foreground(p)==0);
    p.window=63;p.blend=0x81;assert(foreground(p)==0x80ffffffu);
    p.brightness=31;assert(foreground(p)==0xffffffffu);
    // A window which removes BG0 must expose the 2D/background, including
    // backdrop-only regions; these used to leak native geometry.
    p.window=62;assert(foreground(p)==p.resolved);
    // BG0 beats a tied BG; a tied OBJ remains visible above geometry.
    p={};p.resolved=0xffabcdef;p.front_layer=3;p.front_priority=0;
    assert(foreground(p)==0);
    p.front_layer=4;assert(foreground(p)==p.resolved);
    p.front_layer=3;p.geometry_priority=1;assert(foreground(p)==p.resolved);
    // Peach/dialogue 2D alpha must use the live geometry underneath, not a
    // black placeholder. 8/8 keeps half of each; 0/16 and 16/0 are endpoints.
    p.front=0xfff0e0d0;p.blend=0x148;p.alpha=0x0808;
    assert(foreground(p)==0x80f0e0d0u);
    p.alpha=0x1000;assert(foreground(p)==0x00f0e0d0u);
    p.alpha=0x0010;assert(foreground(p)==0xfff0e0d0u);
    p.alpha=0x0808;p.window=31;assert(foreground(p)==p.resolved);
    // Bitmap OBJ uses its own alpha; forced semi OBJ ignores effect-disable.
    p.front_layer=0x84;p.obj_alpha=4;p.blend=0x100;
    assert(foreground(p)==0x40f0e0d0u);
    p.window=30;assert(foreground(p)==p.resolved);
    using sm64ds::render::bridge_policy::geometry_effect_edge;
    assert(geometry_effect_edge(0x70000000,63,0xe1,7));
    assert(!geometry_effect_edge(0xff000000,63,0xe1,7));
    assert(geometry_effect_edge(0,31,0xe1,7));
    assert(!geometry_effect_edge(0,30,0xe1,7));
    // A sky hidden behind a logo must extend alone, with inward-reflected
    // samples at both edges. Never copy that logo or an OBJ to a side strip.
    static std::array<std::uint32_t,256*192> front,back,sky;
    static std::array<std::uint8_t,256*192> fl,bl;
    fl.fill(2);bl.fill(3);front.fill(0xffff0000);
    for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x)back[y*256+x]=0xff000000u|x;
    // No single foreground BG covers all strips; BG3 does.
    for(unsigned y=0;y<192;++y)fl[y*256]=4;
    assert(sm64ds::render::bridge_policy::sky_strips(front.data(),back.data(),fl.data(),bl.data(),sky.data(),0,0,true));
    assert(sky[0]==0xff00001fu&&sky[31]==0xff000000u);
    assert(sky[32]==0xff0000ffu&&sky[63]==0xff0000e0u);
    bl[0]=5;fl[0]=4;
    assert(!sm64ds::render::bridge_policy::sky_strips(front.data(),back.data(),fl.data(),bl.data(),sky.data(),0,0,true));
    // ForegroundTable must equal foreground() for every layer/priority
    // combination (and out-of-range fields) under every blend state shape.
    using sm64ds::render::bridge_policy::ForegroundTable;
    static ForegroundTable table;
    std::uint32_t seed=0x1234567u;
    auto random=[&]{seed=seed*1664525u+1013904223u;return seed;};
    const unsigned blends[]={0,1,0x41,0x81,0xc1,0x101,0x141,0x1c1,0x3f3f,0x2e81,0xe1,0x13f};
    const unsigned alphas[]={0,0x0808,0x1000,0x0010,0x0a06,0x1f1f,0x0f01};
    unsigned checked=0;
    for(unsigned gp=0;gp<5;++gp)for(unsigned blend:blends)for(unsigned alpha:alphas)for(unsigned brightness:{0u,7u,16u,31u}){
        table.configure(gp,blend,alpha,brightness);
        for(unsigned fl=0;fl<9;++fl)for(unsigned semi:{0u,128u})for(unsigned fp=0;fp<6;++fp)for(unsigned bl=0;bl<7;++bl)for(unsigned bp=0;bp<6;++bp){
            Pixel q;q.front=random()|0x01000000u;q.resolved=random();q.front_layer=fl|semi;q.front_priority=fp;
            q.back_layer=bl;q.back_priority=bp;q.geometry_priority=gp;q.window=63;q.obj_alpha=0;
            q.blend=blend;q.alpha=alpha;q.brightness=brightness;
            assert(table.pixel(q.front,q.resolved,q.front_layer,fp,bl,bp)==foreground(q));++checked;
        }
    }
    assert(checked>1000000);
    // BUILD124: windows and per-OBJ alpha use the same table. Every window
    // bit pattern foreground() reads (bits 0 and 5) and own alphas on both
    // sides of 16, mixed in one table so cached forms must not leak between
    // window/alpha combinations of the same layer indices.
    checked=0;
    const unsigned windows[]={0,1,32,33,62,63};
    const unsigned objs[]={0,4,16,17,31};
    for(unsigned gp=0;gp<5;++gp)for(unsigned blend:blends)for(unsigned alpha:{0u,0x0808u,0x0a06u,0x1f1fu})for(unsigned brightness:{0u,16u}){
        table.configure(gp,blend,alpha,brightness);
        for(unsigned fl=0;fl<9;++fl)for(unsigned semi:{0u,128u})for(unsigned fp=0;fp<6;++fp)for(unsigned bl=0;bl<7;++bl)for(unsigned bp=0;bp<6;++bp)
        for(unsigned window:windows)for(unsigned obj:objs){
            Pixel q;q.front=random()|0x01000000u;q.resolved=random();q.front_layer=fl|semi;q.front_priority=fp;
            q.back_layer=bl;q.back_priority=bp;q.geometry_priority=gp;q.window=window;q.obj_alpha=obj;
            q.blend=blend;q.alpha=alpha;q.brightness=brightness;
            assert(table.pixel(q.front,q.resolved,q.front_layer,fp,bl,bp,window,obj)==foreground(q));++checked;
        }
    }
    assert(checked>60000000);
    // The window-free entry point still matches window 63 / alpha 0 after
    // windowed lookups filled the same state's table.
    table.configure(0,0x141,0x0808,0);
    for(unsigned fl=0;fl<8;++fl)for(unsigned fp=0;fp<4;++fp){
        Pixel q;q.front=0xff405060u;q.resolved=0xff102030u;q.front_layer=fl;q.front_priority=fp;q.back_layer=5;q.back_priority=3;
        q.blend=0x141;q.alpha=0x0808;q.window=0;
        assert(table.pixel(q.front,q.resolved,fl,fp,5,3,0,0)==foreground(q));
        q.window=63;assert(table.pixel(q.front,q.resolved,fl,fp,5,3)==foreground(q));
    }
}
