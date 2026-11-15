// BUILD134 PPU cost cuts: two builds of port/ntr/ppu.cpp run side by side
// over the same DS memory, through randomized multi-frame sequences, and
// every output of every preparation is compared: return values, descriptors,
// map rasters, foreground buffers, band hints, candidate planes and their
// CPU composition. The reference build is the BUILD 133 source (or the new
// source compiled with the cuts off); the candidate is the new source.
// See test_ppu_cost_cuts.py for how both are compiled.
#include "ppu_cost_cuts.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {volatile int sm64ds_gpu_affine_ready=0,sm64ds_gpu_affine_wanted=0;char* fake_heap_end=nullptr;}
extern "C" const HarnessSide* harness_side_reference();
extern "C" const HarnessSide* harness_side_candidate();

namespace {
constexpr unsigned W=256,H=192,PIXELS=W*H;
uint8_t* region[8];
const unsigned region_size[8]={0,0,0,0,0x2000,0x800,0x900000,0x800};
uint8_t* mem(uint32_t a){return region[a>>24]+(a&0x00ffffffu);}
void w8(uint32_t a,unsigned v){*mem(a)=uint8_t(v);}
void w16(uint32_t a,unsigned v){const uint16_t h=uint16_t(v);std::memcpy(mem(a),&h,2);}
void w32(uint32_t a,uint32_t v){std::memcpy(mem(a),&v,4);}
unsigned r16(uint32_t a){uint16_t h;std::memcpy(&h,mem(a),2);return h;}
uint32_t r32(uint32_t a){uint32_t v;std::memcpy(&v,mem(a),4);return v;}

struct Rng {
    uint64_t s;
    uint32_t next(){s^=s<<13;s^=s>>7;s^=s<<17;return uint32_t(s>>16);}
    uint32_t below(uint32_t n){return n?next()%n:0;}
    bool chance(unsigned percent){return below(100)<percent;}
    int range(int low,int high){return low+int(below(uint32_t(high-low+1)));}
};

unsigned sequence_id=0,step_id=0;const char* phase_name="";
[[noreturn]] void fail(const char* what,long detail=-1){
    std::fprintf(stderr,"MISMATCH phase=%s sequence=%u step=%u: %s (%ld)\n",phase_name,sequence_id,step_id,what,detail);
    std::exit(1);
}
void same_bytes(const void* a,const void* b,size_t n,const char* what){
    if(!a || !b){if(a!=b)fail(what,-2);return;}
    if(std::memcmp(a,b,n)){
        const auto* x=static_cast<const uint8_t*>(a);const auto* y=static_cast<const uint8_t*>(b);
        size_t i=0;while(x[i]==y[i])++i;
        fail(what,long(i));
    }
}
[[noreturn]] void fail_field(const char* what,const char* field){
    std::fprintf(stderr,"MISMATCH phase=%s sequence=%u step=%u: %s%s\n",phase_name,sequence_id,step_id,what,field);
    std::exit(1);
}
#define SAME(field,what) do{if((a.field)!=(b.field))fail_field(what,#field);}while(0)

// Pointers differ between builds; which of its own buffers a build hands out
// must not (double buffering is part of the output).
struct Slots {
    std::vector<const void*> seen;
    int id(const void* p){
        if(!p)return -1;
        for(size_t i=0;i<seen.size();++i)if(seen[i]==p)return int(i);
        seen.push_back(p);return int(seen.size()-1);
    }
};
struct Side {
    const HarnessSide* api;
    uint32_t* foreground[3];
    uint32_t* composed;
    Slots rasters,overlays,bases;
    double ns=0,idle_ns=0;
};
Side sides[2];
double now_ns(){return double(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}

void same_layer(const HarnessLayer& a,const HarnessLayer& b,const char* what){
    SAME(width,what);SAME(height,what);SAME(raster_generation,what);SAME(ref_x,what);SAME(ref_y,what);
    SAME(pa,what);SAME(pb,what);SAME(pc,what);SAME(pd,what);SAME(wrap,what);SAME(backdrop,what);SAME(layer,what);SAME(priority,what);
    SAME(base_generation,what);SAME(dirty_bands[0],what);SAME(dirty_bands[1],what);SAME(overlay_generation,what);
    SAME(effect_mode,what);SAME(effect_factor,what);SAME(effect_layer,what);SAME(effect_backdrop,what);
    if(sides[0].rasters.id(a.raster)!=sides[1].rasters.id(b.raster))fail(what,-3);
    if(a.raster)same_bytes(a.raster,b.raster,size_t(a.width)*a.height*4,"map raster texels");
    if(sides[0].overlays.id(a.overlay)!=sides[1].overlays.id(b.overlay))fail(what,-4);
    if(a.overlay)same_bytes(a.overlay,b.overlay,PIXELS*4,"overlay plane");
}
void same_view(const HarnessView& a,const HarnessView& b,const char* what){
    SAME(blend,what);SAME(alpha,what);SAME(brightness,what);SAME(master,what);SAME(geometry_priority,what);
    SAME(blank,what);SAME(has_geometry,what);SAME(resolved,what);SAME(has_windows,what);SAME(has_obj_alpha,what);
    SAME(semi_geometry,what);SAME(obj_first_blend,what);SAME(semi_back,what);SAME(bg_first_blend,what);
    SAME(has_cut,what);SAME(cut_color,what);SAME(cut_count,what);SAME(cut_signature,what);SAME(cut_rect_bytes,what);
    if(a.has_cut)same_bytes(a.cut_rects,b.cut_rects,a.cut_rect_bytes,"window cut rectangles");
    SAME(has_identity,what);SAME(background_generation,what);SAME(front_alpha_encoded,what);SAME(has_affine,what);
    if(a.has_affine){same_layer(a.affine,b.affine,what);return;} // planes are not rasterized on this path
    same_bytes(a.front,b.front,PIXELS*4,"front plane");same_bytes(a.back,b.back,PIXELS*4,"back plane");
    same_bytes(a.front_layer,b.front_layer,PIXELS,"front layers");same_bytes(a.front_priority,b.front_priority,PIXELS,"front priorities");
    same_bytes(a.back_layer,b.back_layer,PIXELS,"back layers");same_bytes(a.back_priority,b.back_priority,PIXELS,"back priorities");
    same_bytes(a.window_mask,b.window_mask,PIXELS,"window masks");same_bytes(a.front_obj_alpha,b.front_obj_alpha,PIXELS,"front OBJ alpha");
}
// MASTER_BRIGHT as the PPU's final composition applies it (six-bit channels).
uint32_t master_brightness(uint32_t color,unsigned master){
    const unsigned mode=master>>14,factor=std::min(master&31u,16u);
    if(mode!=1 && mode!=2)return color;
    uint32_t result=0xff000000u;
    for(unsigned shift:{0u,8u,16u}){
        unsigned c=((color>>shift)&255)>>2;
        c=mode==1?c+((63-c)*factor)/16:c-(c*factor+15)/16;
        result|=((c<<2)|(c>>4))<<shift;
    }
    return result;
}
// The picture a native publication stands for: map texel, backdrop,
// foreground, then the master brightness its consumer applies.
void picture(const HarnessLayer& l,const uint32_t* foreground,uint32_t* out,unsigned master){
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        int sx=(l.ref_x+int(x)*l.pa+int(y)*l.pb)>>8,sy=(l.ref_y+int(x)*l.pc+int(y)*l.pd)>>8;
        if(l.wrap){sx&=l.width-1;sy&=l.height-1;}
        uint32_t pixel=0;
        if(unsigned(sx)<unsigned(l.width)&&unsigned(sy)<unsigned(l.height))pixel=l.raster[unsigned(sy)*l.width+unsigned(sx)];
        if(!(pixel>>24))pixel=l.backdrop;
        if(foreground[y*W+x]>>24)pixel=foreground[y*W+x];
        out[y*W+x]=master_brightness(pixel,master);
    }
}

// ---- DS state -------------------------------------------------------------
constexpr uint32_t B=0x04001000,B_PLTT=0x05000400,B_BG=0x06200000,B_OBJ=0x06600000,B_OAM=0x07000400;
constexpr uint32_t A=0x04000000,A_PLTT=0x05000000,A_BG=0x06000000,A_OBJ=0x06400000,A_OAM=0x07000000;
std::vector<uint8_t> saved[8]; // memory at the start of the sequence (for reverts)

void fill(Rng& r,uint32_t address,size_t bytes,unsigned zero_one_in){
    uint8_t* p=mem(address);
    for(size_t i=0;i<bytes;++i)p[i]=(zero_one_in && !r.below(zero_one_in))?0:uint8_t(r.next());
}
struct Sprite {int x,y;unsigned shape,size,mode,tile,palette,priority;bool c256,affine,dbl,hflip,vflip,off;unsigned group;};
void put_sprite(uint32_t oam,unsigned i,const Sprite& s){
    unsigned a0=unsigned(s.y)&0xff,a1=unsigned(s.x)&0x1ff,a2=(s.tile&0x3ff)|(s.priority<<10)|(s.palette<<12);
    a0|=(s.mode<<10)|(s.c256?0x2000u:0u)|(s.shape<<14);
    if(s.affine)a0|=0x100u|(s.dbl?0x200u:0u);else if(s.off)a0|=0x200u;
    a1|=s.size<<14;
    if(s.affine)a1|=s.group<<9;else a1|=(s.hflip?0x1000u:0u)|(s.vflip?0x2000u:0u);
    w16(oam+i*8,a0);w16(oam+i*8+2,a1);w16(oam+i*8+4,a2);
}
Sprite random_sprite(Rng& r,unsigned max_priority,unsigned semi_percent,unsigned bitmap_percent){
    Sprite s{};
    s.x=r.range(-40,270);s.y=r.range(-30,200);
    s.shape=r.below(3);s.size=r.chance(70)?r.below(3):3;
    s.mode=r.chance(semi_percent)?1:r.chance(bitmap_percent)?3:r.chance(2)?2:0;
    s.tile=r.chance(80)?r.below(96):r.below(1024);s.palette=r.below(16);
    s.priority=r.chance(92)?r.below(max_priority+1):r.below(4);
    s.c256=r.chance(25);s.affine=r.chance(20);s.dbl=r.chance(40);s.hflip=r.chance(30);s.vflip=r.chance(30);s.off=false;
    s.group=r.below(32);
    return s;
}
void random_matrix(Rng& r,uint32_t oam,unsigned group){
    const int scale=r.range(150,400),turn=r.range(-200,200);
    w16(oam+(group*4+0)*8+6,unsigned(scale));w16(oam+(group*4+1)*8+6,unsigned(turn));
    w16(oam+(group*4+2)*8+6,unsigned(-turn));w16(oam+(group*4+3)*8+6,unsigned(scale));
}
struct Scenario {
    unsigned sprites=0;Sprite sprite[128];
    unsigned map_priority=2;
    unsigned semi_percent=0,bitmap_percent=0;
};
Scenario scenario_b,scenario_a;

// BG3 of engine B as read_bg sees it (extended tiled).
uint32_t map_screen(){return B_BG+(((r16(B+14)>>8)&0x1f)<<11);}
uint32_t map_chars(){return B_BG+(((r16(B+14)>>2)&15)<<14);}
unsigned map_entries(){const unsigned side=16u<<(r16(B+14)>>14);return side*side;}

void setup_b(Rng& r,Scenario& sc){
    sc=Scenario{};
    std::memset(mem(B),0,0x70);
    // Display: mode 3 (BG3 extended), OBJ, usually 1D OBJ mapping.
    uint32_t disp=3u|0x800u|0x10000u;
    if(r.chance(75))disp|=0x10u;
    disp|=r.below(4)<<20;
    if(r.chance(92))disp|=0x1000u;
    if(r.chance(30))disp|=0x100u;
    if(r.chance(25))disp|=0x200u;
    if(r.chance(50))disp|=1u<<30;
    if(r.chance(25))disp|=1u<<31;
    w32(B,disp);
    sc.map_priority=r.chance(80)?r.range(1,3):0;
    const unsigned size=r.chance(60)?0:r.chance(70)?1:r.chance(85)?2:3;
    const unsigned char_base=r.chance(80)?4:r.below(5),screen_base=r.chance(60)?0x1f:r.below(32);
    w16(B+14,sc.map_priority|(char_base<<2)|(screen_base<<8)|(r.chance(50)?0x2000u:0u)|(size<<14));
    for(unsigned bg=0;bg<2;++bg){
        const unsigned priority=r.chance(90)?r.below(sc.map_priority+1):r.below(4);
        w16(B+8+bg*2,priority|(r.below(3)<<2)|(r.chance(25)?0x80u:0u)|((24+r.below(4))<<8)|((r.chance(80)?0:r.below(4))<<14));
        w16(B+0x10+bg*4,r.below(512));w16(B+0x12+bg*4,r.below(512));
    }
    // Affine parameters: near identity, some rotation.
    const int scale=r.range(200,320),turn=r.range(-60,60);
    w16(B+0x30,unsigned(scale));w16(B+0x32,unsigned(turn));w16(B+0x34,unsigned(-turn));w16(B+0x36,unsigned(scale));
    w32(B+0x38,uint32_t(r.range(-20000,20000)));w32(B+0x3c,uint32_t(r.range(-20000,20000)));
    // Effects: mostly none; some OBJ blends against the map or the backdrop.
    unsigned blend=0;
    if(r.chance(45)){
        const unsigned mode=r.chance(85)?1:r.range(2,3);
        blend=(mode<<6)|(r.chance(70)?0x0800u:0u)|(r.chance(50)?0x2000u:0u);
        if(r.chance(8))blend|=1u<<r.below(6);
        if(r.chance(5))blend|=0x100u<<r.below(5);
    }
    w16(B+0x50,blend);w16(B+0x52,r.below(17)|(r.below(17)<<8));w16(B+0x54,r.below(17));
    w16(B+0x6c,r.chance(90)?0:(r.below(17)|(r.range(1,2)<<14)));
    w8(0x04000248,r.chance(90)?0x82:r.below(256));w8(0x04000249,r.chance(90)?0x83:r.below(256));
    fill(r,B_PLTT,0x400,0);fill(r,0x06898000,0x8000,0);fill(r,0x068a0000,0x2000,0);
    fill(r,B_BG,0x20000,5);fill(r,B_OBJ,0x20000,3);
    // Maps name a limited set of tiles, so most of a character block is unused.
    const unsigned tiles=r.chance(70)?r.range(8,200):1024;
    for(uint32_t at=B_BG;at<B_BG+0x20000;at+=2)w16(at,(r.below(tiles))|(r.below(4)<<10)|(r.below(r.chance(50)?2:16)<<12));
    fill(r,map_chars(),0x10000,5); // the map block again: the loop above wrote entries over it
    for(unsigned i=0;i<128;++i){w16(B_OAM+i*8,0x200);w16(B_OAM+i*8+2,0);w16(B_OAM+i*8+4,0);w16(B_OAM+i*8+6,0);}
    for(unsigned g=0;g<32;++g)random_matrix(r,B_OAM,g);
    sc.semi_percent=r.chance(35)?r.range(5,40):0;sc.bitmap_percent=r.chance(12)?10:0;
    sc.sprites=r.chance(10)?0:r.range(1,28);
    for(unsigned i=0;i<sc.sprites;++i){sc.sprite[i]=random_sprite(r,sc.map_priority,sc.semi_percent,sc.bitmap_percent);put_sprite(B_OAM,r.chance(70)?i:i*4%128,sc.sprite[i]);}
}
void setup_a(Rng& r,Scenario& sc){
    sc=Scenario{};
    std::memset(mem(A),0,0x70);
    uint32_t disp=0x10000u|0x1000u|0x200u;      // text BG1 + OBJ
    if(r.chance(75))disp|=0x10u;
    disp|=r.below(4)<<20;
    if(r.chance(40))disp|=0x8u|0x100u;          // BG0 is the 3D layer
    if(r.chance(30))disp|=0x400u;
    if(r.chance(10))disp|=0x2000u;              // WIN0
    w32(A,disp);
    w16(A+8,r.below(4));
    w16(A+10,r.below(4)|(1u<<2)|(2u<<8));w16(A+12,r.below(4)|(2u<<2)|(3u<<8));
    w16(A+0x40,r.below(65536));w16(A+0x44,r.below(65536));w16(A+0x48,r.below(64));w16(A+0x4a,r.below(64));
    unsigned blend=0;
    if(r.chance(55)){
        const unsigned mode=r.chance(80)?1:r.range(2,3);
        blend=(mode<<6)|(r.below(64)&(r.chance(50)?0x10u:0x3fu))|((r.below(64))<<8);
    }
    w16(A+0x50,blend);w16(A+0x52,r.below(17)|(r.below(17)<<8));w16(A+0x54,r.below(17));
    w16(A+0x6c,r.chance(85)?0:(r.below(17)|(r.range(1,2)<<14)));
    fill(r,A_PLTT,0x400,0);fill(r,A_BG,0x10000,5);fill(r,A_OBJ,0x20000,3);
    for(uint32_t at=A_BG+0x1000;at<A_BG+0x2000;at+=2)w16(at,r.below(64)|(r.below(4)<<10)|(r.below(16)<<12));
    for(unsigned i=0;i<128;++i){w16(A_OAM+i*8,0x200);w16(A_OAM+i*8+2,0);w16(A_OAM+i*8+4,0);w16(A_OAM+i*8+6,0);}
    for(unsigned g=0;g<32;++g)random_matrix(r,A_OAM,g);
    sc.map_priority=3;sc.semi_percent=r.chance(60)?r.range(5,50):0;sc.bitmap_percent=r.chance(10)?8:0;
    sc.sprites=r.range(2,40);
    for(unsigned i=0;i<sc.sprites;++i){sc.sprite[i]=random_sprite(r,3,sc.semi_percent,sc.bitmap_percent);put_sprite(A_OAM,i,sc.sprite[i]);}
}
void snapshot(){for(unsigned i:{4u,5u,6u,7u}){saved[i].resize(i==6?0x8c0000:region_size[i]);std::memcpy(saved[i].data(),region[i],saved[i].size());}}
void revert(uint32_t address,size_t bytes){std::memcpy(mem(address),saved[address>>24].data()+(address&0x00ffffffu),bytes);}

// OBJ mutations shared by both engines. Returns a short tag.
const char* mutate_obj(Rng& r,Scenario& sc,uint32_t oam,uint32_t obj,uint32_t pltt,uint32_t reg,uint32_t extended){
    const unsigned kind=r.below(100);
    if(kind<40){
        // Move one to three live sprites a few pixels (sometimes far away).
        unsigned live[128],count=0;
        for(unsigned k=0;k<128;++k){const unsigned a0=r16(oam+k*8);if((a0&0x300)!=0x200)live[count++]=k;}
        if(!count)return "idle";
        const unsigned moves=r.range(1,3);
        for(unsigned m=0;m<moves;++m){
            const unsigned k=live[r.below(count)];
            unsigned a0=r16(oam+k*8),a1=r16(oam+k*8+2);
            int x=int(a1&0x1ff),y=int(a0&0xff);
            x+=r.range(-6,6);y+=r.range(-6,6);
            if(r.chance(3)){x=r.range(0,511);y=r.range(0,255);}
            w16(oam+k*8,(a0&~0xffu)|(unsigned(y)&0xff));w16(oam+k*8+2,(a1&~0x1ffu)|(unsigned(x)&0x1ff));
        }
        return "obj move";
    }
    if(kind<52){const unsigned k=r.below(128);
        switch(r.below(7)){
        case 0:w16(oam+k*8+4,r16(oam+k*8+4)^(1u<<r.below(10)));break;                  // tile
        case 1:w16(oam+k*8+4,(r16(oam+k*8+4)&0x0fffu)|(r.below(16)<<12));break;         // palette
        case 2:w16(oam+k*8+4,(r16(oam+k*8+4)&~0x0c00u)|(r.below(sc.map_priority+1)<<10));break; // priority
        case 3:w16(oam+k*8+2,r16(oam+k*8+2)^(r.chance(50)?0x1000u:0x2000u));break;     // flips / affine group
        case 4:w16(oam+k*8,r16(oam+k*8)^0x200u);break;                                  // on/off, double size
        case 5:w16(oam+k*8,r16(oam+k*8)^(r.chance(sc.semi_percent?60:5)?0x400u:r.chance(50)?0x2000u:0x100u));break; // semi / colours / affine
        default:put_sprite(oam,r.below(128),random_sprite(r,sc.map_priority,sc.semi_percent,sc.bitmap_percent));
        }
        return "obj attribute";
    }
    if(kind<60){random_matrix(r,oam,r.below(32));return "obj matrix";}
    if(kind<70){w16(pltt+0x200+r.below(256)*2,r.below(32768));return "obj palette";}
    if(kind<82){ // a tile byte some sprite may sample
        const unsigned k=r.below(128);const unsigned tile=r16(oam+k*8+4)&0x3ff;
        const uint32_t boundary=(r32(reg)&0x10)?32u<<((r32(reg)>>20)&3):32u;
        const uint32_t at=(tile*boundary+r.below(4096))&0x1ffffu;
        w8(obj+at,r.chance(20)?0:r.next());
        return "obj tile";
    }
    if(kind<88){w8(obj+r.below(0x20000),r.next());return "obj vram";}
    if(kind<92 && extended){w8(extended+r.below(0x2000),r.next());return "obj extended palette";}
    if(kind<96){revert(oam,0x400);return "obj revert oam";}
    if(r.chance(50))revert(pltt+0x200,0x200);else revert(obj,0x20000);
    return "obj revert";
}
struct Weights {unsigned idle,obj,rotate,tiles,palette,map,registers,overlay,revert;};
const char* mutate_b(Rng& r,Scenario& sc,const Weights& w,bool& unused_tile){
    unused_tile=false;
    const unsigned total=w.idle+w.obj+w.rotate+w.tiles+w.palette+w.map+w.registers+w.overlay+w.revert;
    unsigned pick=r.below(total);
    if(pick<w.idle)return "idle";pick-=w.idle;
    if(pick<w.obj)return mutate_obj(r,sc,B_OAM,B_OBJ,B_PLTT,B,0x068a0000);pick-=w.obj;
    if(pick<w.rotate){
        switch(r.below(4)){
        case 0:w32(B+0x38,r32(B+0x38)+uint32_t(r.range(-900,900)));break;
        case 1:w32(B+0x3c,r32(B+0x3c)+uint32_t(r.range(-900,900)));break;
        case 2:{const int scale=r.range(180,340),turn=r.range(-90,90);
            w16(B+0x30,unsigned(scale));w16(B+0x32,unsigned(turn));w16(B+0x34,unsigned(-turn));w16(B+0x36,unsigned(scale));break;}
        default:w32(B+0x38,r32(B+0x38)+256u);w32(B+0x3c,r32(B+0x3c)+uint32_t(r.range(-300,300)));
        }
        return "map affine";
    }pick-=w.rotate;
    if(pick<w.tiles){
        // Which tiles does the map name right now?
        bool used[1024]{};const unsigned entries=map_entries();
        for(unsigned i=0;i<entries && i<4096;++i)used[r16(map_screen()+i*2)&0x3ff]=true;
        unsigned candidates[1024],count=0;const bool want_used=r.chance(45);
        for(unsigned t=0;t<1024;++t)if(used[t]==want_used)candidates[count++]=t;
        if(!count)return "idle";
        const unsigned tile=candidates[r.below(count)];
        const unsigned bytes=r.chance(70)?1:r.range(2,64);
        for(unsigned i=0;i<bytes;++i)w8(map_chars()+tile*64+r.below(64),r.chance(15)?0:r.next());
        unused_tile=!want_used;
        return want_used?"map tile (named)":"map tile (unnamed)";
    }pick-=w.tiles;
    if(pick<w.palette){
        switch(r.below(4)){
        case 0:w16(B_PLTT+r.below(256)*2,r.below(32768));return "bg palette";
        case 1:w16(0x06898000+r.below(0x4000)*2,r.below(32768));return "bg extended palette";
        case 2:w16(0x06898000+0x6000+r.below(0x1000)*2,r.below(32768));return "bg extended palette (map slot)";
        default:w16(B_PLTT,r.below(32768));return "backdrop";
        }
    }pick-=w.palette;
    if(pick<w.map){
        const unsigned entries=map_entries();const unsigned changes=r.chance(70)?1:r.range(2,40);
        for(unsigned i=0;i<changes;++i){const uint32_t at=map_screen()+r.below(std::min(entries,4096u))*2;
            w16(at,r.chance(60)?(r16(at)^(1u<<r.below(16))):(r.below(1024)|(r.below(4)<<10)|(r.below(16)<<12)));}
        return "map entry";
    }pick-=w.map;
    if(pick<w.registers){
        switch(r.below(12)){
        case 0:w16(B+0x50,r16(B+0x50)^(1u<<r.below(14)));return "BLDCNT";
        case 1:w16(B+0x52,r.below(17)|(r.below(17)<<8));return "BLDALPHA";
        case 2:w16(B+0x54,r.below(17));return "BLDY";
        case 3:w16(B+0x6c,r.chance(60)?0:(r.below(17)|(r.range(1,2)<<14)));return "MASTER_BRIGHT";
        case 4:w32(B,r32(B)^(r.chance(50)?0x100u:0x200u));return "DISPCNT overlay";
        case 5:w32(B,r32(B)^0x1000u);return "DISPCNT OBJ";
        case 6:w32(B,r32(B)^(r.chance(50)?(1u<<30):(1u<<31)));return "DISPCNT extended palettes";
        case 7:w32(B,r32(B)^(r.chance(50)?0x10u:(1u<<(20+r.below(2)))));return "DISPCNT OBJ mapping";
        case 8:{const unsigned k=r.below(6);const uint32_t d=r32(B);
            w32(B,k==0?d^0x10000u:k==1?d^0x80u:k==2?d^0x2000u:k==3?d^0x800u:k==4?(d&~7u)|r.below(6):d^0x400u);return "DISPCNT display";}
        case 9:{const unsigned cnt=r16(B+14);const unsigned k=r.below(5);
            w16(B+14,k==0?cnt^0x2000u:k==1?(cnt&~3u)|r.below(4):k==2?(cnt&0x3fffu)|(r.below(4)<<14):k==3?cnt^(1u<<(8+r.below(5))):cnt^(1u<<(2+r.below(4))));
            sc.map_priority=r16(B+14)&3;return "BG3CNT";}
        case 10:w8(0x04000248+r.below(2),r.chance(50)?(r.chance(50)?0x82:0x83):r.below(256));return "VRAMCNT";
        default:w32(B+0x28+r.below(2)*4,r.next());return "unused BG2 affine";
        }
    }pick-=w.registers;
    if(pick<w.overlay){
        const unsigned bg=r.below(2);const unsigned cnt=r16(B+8+bg*2);
        const uint32_t screen=B_BG+(((cnt>>8)&0x1f)<<11),chars=B_BG+(((cnt>>2)&15)<<14);
        switch(r.below(5)){
        case 0:w16(B+0x10+bg*4+r.below(2)*2,r.below(512));return "overlay scroll";
        case 1:w16(screen+r.below(1024)*2,r.below(1024)|(r.below(4)<<10)|(r.below(16)<<12));return "overlay entry";
        case 2:w8(chars+r.below(0x8000),r.next());return "overlay tile";
        case 3:w16(B+8+bg*2,cnt^(1u<<r.below(16)));return "overlay BGCNT";
        default:w16(B+8+bg*2,(cnt&~3u)|r.below(sc.map_priority+1));return "overlay priority";
        }
    }pick-=w.overlay;
    switch(r.below(7)){
    case 0:revert(B,0x70);sc.map_priority=r16(B+14)&3;return "revert registers";
    case 1:revert(B_PLTT,0x200);return "revert bg palette";
    case 2:revert(0x06898000,0x8000);return "revert bg extended palette";
    case 3:revert(B_BG,0x20000);return "revert bg vram";
    case 4:revert(map_chars(),0x10000);return "revert map characters";
    case 5:revert(B+0x30,16);return "revert map affine";
    default:revert(map_screen(),std::min(map_entries(),4096u)*2);return "revert map";
    }
}
const char* mutate_a(Rng& r,Scenario& sc){
    const unsigned kind=r.below(100);
    if(kind<12)return "idle";
    if(kind<82)return mutate_obj(r,sc,A_OAM,A_OBJ,A_PLTT,A,0);
    if(kind<86){w16(A+0x50,r16(A+0x50)^(1u<<r.below(14)));return "BLDCNT";}
    if(kind<89){w16(A+0x52,r.below(17)|(r.below(17)<<8));return "BLDALPHA";}
    if(kind<91){w16(A+0x10+4+r.below(2)*2,r.below(512));return "bg scroll";}
    if(kind<93){w16(A_BG+0x1000+r.below(1024)*2,r.below(64)|(r.below(16)<<12));return "bg entry";}
    if(kind<95){w16(A_PLTT+r.below(256)*2,r.below(32768));return "bg palette";}
    if(kind<97){w32(A,r32(A)^(r.chance(40)?0x1000u:r.chance(50)?0x200u:0x2000u));return "DISPCNT";}
    if(kind<98){w16(A+0x6c,r.chance(60)?0:(r.below(17)|(r.range(1,2)<<14)));return "MASTER_BRIGHT";}
    revert(A,0x70);return "revert registers";
}

// ---- comparison steps -------------------------------------------------------
struct Totals {
    unsigned long long steps=0,native_true=0,native_false=0,foreground_bytes=0,raster_bytes=0,plane_bytes=0,
        unnamed_tile_hits=0,idle_steps=0,descriptor_differences=0,return_differences=0,candidate_later=0,pictures_not_compared=0;
} totals;

void compare_candidates(int engine,bool strict){
    HarnessView view[2];
    for(unsigned s=0;s<2;++s)sides[s].api->candidates(engine,&view[s],sides[s].composed);
    if(strict){
        same_view(view[0],view[1],"candidate view: ");
        if(!view[0].has_affine)totals.plane_bytes+=PIXELS*14ull;
    }
    if(!view[0].has_affine && !view[1].has_affine){
        same_bytes(sides[0].composed,sides[1].composed,PIXELS*4,"CPU composition");totals.plane_bytes+=PIXELS*4ull;
    }
}
// One update of the bottom-screen map as present_playable prepares it.
void step_b(unsigned buffer,bool strict,bool idle,bool unnamed_tile){
    HarnessLayer layer[2];int ok[2];
    unsigned long long before[8]{};
    if(sides[1].api->stats)std::memcpy(before,sides[1].api->stats,sizeof before);
    for(unsigned s=0;s<2;++s){
        const double start=now_ns();
        ok[s]=sides[s].api->native(sides[s].foreground[buffer],&layer[s]);
        const double spent=now_ns()-start;
        sides[s].ns+=spent;if(idle)sides[s].idle_ns+=spent;
    }
    if(idle)++totals.idle_steps;
    if(strict){
        if(ok[0]!=ok[1])fail("ppu_prepare_native_affine return value",ok[1]);
        if(ok[0]){
            same_layer(layer[0],layer[1],"native map descriptor: ");
            totals.raster_bytes+=size_t(layer[0].width)*layer[0].height*4ull;
        }
        // The buffer just prepared every step; the others every eighth step.
        for(unsigned b=0;b<3;++b)if(b==buffer || (totals.steps&7)==0){
            same_bytes(sides[0].foreground[b],sides[1].foreground[b],PIXELS*4,"foreground buffer");totals.foreground_bytes+=PIXELS*4ull;
            const uint32_t* base[2];uint64_t generation[2];uint32_t bands[2];
            for(unsigned s=0;s<2;++s)bands[s]=sides[s].api->bands(sides[s].foreground[b],&base[s],&generation[s]);
            if(bands[0]!=bands[1])fail("foreground bands",long(bands[1]));
            if(generation[0]!=generation[1])fail("foreground base generation");
            if(sides[0].bases.id(base[0])!=sides[1].bases.id(base[1]))fail("foreground base identity");
            if(base[0])same_bytes(base[0],base[1],PIXELS*4,"foreground base plane");
        }
        if(sides[1].api->stats && unnamed_tile && ok[1] && sides[1].api->stats[1]>before[1])++totals.unnamed_tile_hits;
    }else{
        // Disturbed layer caches: the builds may differ in the generation
        // they publish under, or one may refuse this update (the BUILD 133
        // full path after another configuration's colour snapshot) and
        // publish on the next; never in the picture a publication stands for.
        static uint32_t image[2][PIXELS];
        const bool differs=ok[0]!=ok[1];
        if(differs){++totals.return_differences;if(!ok[1])++totals.candidate_later;}
        if(ok[0] && ok[1] && (layer[0].raster_generation!=layer[1].raster_generation || layer[0].base_generation!=layer[1].base_generation))++totals.descriptor_differences;
        if(differs){
            // The build that refused publishes this same DS state on its next update.
            const unsigned late=ok[0]?1u:0u;
            ok[late]=sides[late].api->native(sides[late].foreground[buffer],&layer[late]);
            if(!ok[late])++totals.pictures_not_compared;
        }
        if(ok[0] && ok[1]){
            for(unsigned s=0;s<2;++s)picture(layer[s],sides[s].foreground[buffer],image[s],r16(B+0x6c));
            if(std::memcmp(image[0],image[1],sizeof image[0])){
                unsigned count=0,first=PIXELS;
                for(unsigned i=0;i<PIXELS;++i)if(image[0][i]!=image[1][i]){if(first==PIXELS)first=i;++count;}
                std::fprintf(stderr,"picture: DISPCNT=%08x BLDCNT=%04x BLDALPHA=%04x BG3CNT=%04x MASTER_BRIGHT=%04x VRAMCNT_H/I=%02x/%02x pixels=%u first=%u,%u reference=%08x candidate=%08x\n",
                    r32(B),r16(B+0x50),r16(B+0x52),r16(B+14),r16(B+0x6c),*mem(0x04000248),*mem(0x04000249),count,first%W,first/W,image[0][first],image[1][first]);
            }
            same_bytes(image[0],image[1],sizeof image[0],differs?"picture (publication against the other build's next publication)":"picture");
            totals.foreground_bytes+=PIXELS*4ull;
        }else if(!ok[0] && !ok[1]){
            // Both show the CPU composition. Its per-sprite OBJ layer keeps
            // history that extended OBJ palette changes do not invalidate
            // (pinned code), and the two builds no longer share a history:
            // compared only with that layer off (PPU_NO_SPRITE_INCREMENTAL=1).
            static const bool exact_cpu=std::getenv("PPU_NO_SPRITE_INCREMENTAL")!=nullptr;
            for(unsigned s=0;s<2;++s){HarnessView view;sides[s].api->candidates(1,&view,sides[s].composed);if(view.has_affine)fail("fallback without planes");}
            if(exact_cpu){same_bytes(sides[0].composed,sides[1].composed,PIXELS*4,"CPU composition of a refused publication");totals.foreground_bytes+=PIXELS*4ull;}
        }
    }
    if(ok[0])++totals.native_true;else ++totals.native_false;
    // The runtime prepares the CPU candidates when the publication is refused.
    if(strict && !ok[0])compare_candidates(1,true);
    ++totals.steps;
}

void run_b(uint64_t seed,unsigned sequences,unsigned steps,bool disturbed){
    phase_name=disturbed?"B-disturbed":"B";
    for(unsigned q=0;q<sequences;++q){
        sequence_id=q;Rng r{seed+q*0x9e3779b97f4a7c15ull+1};
        setup_b(r,scenario_b);snapshot();
        Weights w{};
        switch(q%6){
        case 0:w={60,12,4,8,3,3,3,2,5};break;      // mostly held frames
        case 1:w={10,60,5,5,3,3,4,3,7};break;      // sprites move
        case 2:w={10,15,55,5,2,3,3,2,5};break;     // the map turns
        case 3:w={10,10,5,45,8,10,4,3,5};break;    // tiles animate
        case 4:w={10,10,10,10,10,10,20,10,10};break; // everything
        default:w={25,25,20,10,3,5,5,2,5};
        }
        unsigned buffer=0;
        for(unsigned k=0;k<steps;++k){
            step_id=k;
            for(unsigned s=0;s<2;++s)sides[s].api->pin(0);
            bool unnamed=false;
            const char* tag=mutate_b(r,scenario_b,w,unnamed);
            const bool idle=!std::strcmp(tag,"idle");
            // The runtime alternates two stores; sometimes it repeats one or uses a third.
            const unsigned choice=r.below(100);
            buffer=choice<88?(buffer^1u)&1u:choice<96?buffer:2u;
            if(buffer==2 && r.chance(50))buffer=r.below(3);
            if(r.chance(3))continue; // an update without a publication (menus)
            step_b(buffer,!disturbed,idle,unnamed);
            if(disturbed && r.chance(12)){
                // Another configuration borrows the BG-only layer caches the
                // affine memo was built over (the title screen's engine B),
                // then the map registers come back, sometimes with a tile
                // no map entry names changed meanwhile.
                uint8_t registers[0x70],palette[0x200];std::memcpy(registers,mem(B),sizeof registers);std::memcpy(palette,mem(B_PLTT),sizeof palette);
                const unsigned extended_at=0x6000+r.below(0x1000)*2;const unsigned extended_colour=r16(0x06898000+extended_at);
                if(r.chance(50))w32(B,(r32(B)&~7u)|(r.chance(50)?0u:1u));  // BG3 becomes a text layer: another layout
                else w32(B,(r32(B)&~7u)|5u|0x400u);                       // a second extended BG: BG3 keeps its layout
                if(r.chance(60)){for(unsigned i=0;i<256;++i)w16(B_PLTT+i*2,r16(B_PLTT+i*2)^0x21);w16(0x06898000+extended_at,extended_colour^0x21);}
                HarnessView view;for(unsigned s=0;s<2;++s)sides[s].api->background(1,&view);
                std::memcpy(mem(B_PLTT),palette,sizeof palette);w16(0x06898000+extended_at,extended_colour);
                std::memcpy(mem(B),registers,sizeof registers);
                if(r.chance(50)){bool unnamed_now=false;const Weights tiles{0,0,0,1,0,0,0,0,0};mutate_b(r,scenario_b,tiles,unnamed_now);}
            }
            else if(!disturbed && r.chance(4))compare_candidates(1,true); // the CPU candidates of the same frame
            for(unsigned s=0;s<2;++s)sides[s].api->pin(1);
        }
    }
}
void run_a(uint64_t seed,unsigned sequences,unsigned steps){
    phase_name="A";
    for(unsigned q=0;q<sequences;++q){
        sequence_id=q;Rng r{seed+q*0xda942042e4dd58b5ull+7};
        setup_a(r,scenario_a);snapshot();
        for(unsigned k=0;k<steps;++k){
            step_id=k;
            for(unsigned s=0;s<2;++s)sides[s].api->pin(0);
            mutate_a(r,scenario_a);
            const int unchanged[2]={sides[0].api->unchanged(0),sides[1].api->unchanged(0)};
            if(unchanged[0]!=unchanged[1])fail("ppu_prepared_inputs_unchanged");
            compare_candidates(0,true);
            ++totals.steps;
            if(r.chance(70))for(unsigned s=0;s<2;++s)sides[s].api->pin(1);
        }
    }
}
}
int main(int argc,char** argv){
    const unsigned sequences=argc>1?unsigned(std::atoi(argv[1])):240;
    const unsigned steps=argc>2?unsigned(std::atoi(argv[2])):300;
    const uint64_t seed=argc>3?std::strtoull(argv[3],nullptr,0):20261114ull;
    for(unsigned i:{4u,5u,6u,7u}){region[i]=static_cast<uint8_t*>(std::calloc(1,region_size[i]));if(!region[i])return 2;}
    sides[0].api=harness_side_reference();sides[1].api=harness_side_candidate();
    for(auto& side:sides){
        side.api->init(region);
        for(auto& buffer:side.foreground)buffer=static_cast<uint32_t*>(std::calloc(PIXELS,4));
        side.composed=static_cast<uint32_t*>(std::calloc(PIXELS,4));
    }
    run_b(seed,sequences,steps,false);
    const Totals strict=totals;
    const double reference_ns=sides[0].ns,candidate_ns=sides[1].ns,reference_idle=sides[0].idle_ns,candidate_idle=sides[1].idle_ns;
    run_a(seed,std::max(1u,sequences/4),steps);
    const Totals with_a=totals;
    run_b(seed^0x5555,std::max(1u,sequences/6),steps,true);
    std::printf("PASS: %llu engine-B publications (%llu published, %llu refused) and %llu engine-A preparations bit-identical: "
        "%llu foreground bytes, %llu map raster bytes, %llu candidate plane bytes\n",
        strict.steps,strict.native_true,strict.native_false,with_a.steps-strict.steps,
        strict.foreground_bytes,strict.raster_bytes,with_a.plane_bytes);
    std::printf("disturbed layer caches: %llu publications, same picture in all; %llu under another raster generation; "
        "%llu refused by one build only and published by it on its next update (reference %llu, candidate %llu; %llu refused again, not compared)\n",
        totals.steps-with_a.steps,totals.descriptor_differences,totals.return_differences,totals.return_differences-totals.candidate_later,totals.candidate_later,totals.pictures_not_compared);
    if(const auto* stats=sides[1].api->stats)
        std::printf("cuts taken: memo planned compares=%llu equal=%llu plans=%llu (hits over a changed unnamed tile=%llu); OBJ kept=%llu row raster=%llu scan_obj=%llu; engine-A word scans=%llu\n",
            stats[0],stats[1],stats[2],strict.unnamed_tile_hits,stats[3],stats[4],stats[5],stats[6]);
    std::printf("host time in ppu_prepare_native_affine (strict phase): reference %.1f ms, candidate %.1f ms; held frames (%llu): reference %.1f ms, candidate %.1f ms\n",
        reference_ns/1e6,candidate_ns/1e6,strict.idle_steps,reference_idle/1e6,candidate_idle/1e6);
    return 0;
}
