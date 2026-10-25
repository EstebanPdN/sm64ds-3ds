// Replay captured 2D inputs only. These offline variants do not alter a save,
// live game RAM, actor, physics clock, or emulator state.
#include "ntr/ppu.h"
#include "ntr/mmio.h"
#include "ntr/vram_bus.h"
#include "affine_overlay_key.hpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <vector>
extern "C" {volatile int sm64ds_gpu_affine_ready=1,sm64ds_gpu_affine_wanted=0;}
namespace ntr {
uint8_t* g_region[8]{};bool g_region_identity[8]{};
bool io_ready(){return true;}
uint64_t io_read(uint32_t address,unsigned size){uint64_t value=0;std::memcpy(&value,host_ptr(address),size);return value;}
void gx_render(Framebuffer&){std::abort();}
VramBanks* io_vram_banks(){return nullptr;}
std::optional<PpuPreparedCandidates> ppu_prepare_affine_alpha_objects(Engine);
}
static unsigned r16(unsigned a){uint16_t v;std::memcpy(&v,ntr::host_ptr(a),2);return v;}
static unsigned r32(unsigned a){uint32_t v;std::memcpy(&v,ntr::host_ptr(a),4);return v;}
static void w16(unsigned a,unsigned v){uint16_t h=v;std::memcpy(ntr::host_ptr(a),&h,2);}
static void w32(unsigned a,unsigned v){std::memcpy(ntr::host_ptr(a),&v,4);}
static constexpr unsigned base=0x04001000,oam=0x07000400;
static ntr::Framebuffer geometry{},actual{},reference{},scanout{};
static void rasterize(const ntr::PpuGpuAffineLayer& l){
    assert(!l.effect_mode);
    for(int y=0;y<ntr::SCREEN_H;++y)for(int x=0;x<ntr::SCREEN_W;++x){
        int sx=(l.ref_x+l.pa*x+l.pb*y)>>8,sy=(l.ref_y+l.pc*x+l.pd*y)>>8;
        if(l.wrap){sx&=l.width-1;sy&=l.height-1;}
        uint32_t c=0;
        if(unsigned(sx)<unsigned(l.width) && unsigned(sy)<unsigned(l.height))c=l.raster[sy*l.width+sx];
        if(!(c>>24))c=l.backdrop;
        if(l.overlay && (l.overlay[y*ntr::SCREEN_W+x]>>24))c=l.overlay[y*ntr::SCREEN_W+x];
        actual.px[y][x]=c|0xff000000u;
    }
}
static void equal(const ntr::Framebuffer& a,const ntr::Framebuffer& b,unsigned k,const char* label){
    for(unsigned y=0;y<ntr::SCREEN_H;++y)for(unsigned x=0;x<ntr::SCREEN_W;++x)
        if(a.px[y][x]!=b.px[y][x]){
            std::fprintf(stderr,"case=%u oracle=%s pixel=%u,%u actual=%08x reference=%08x\n",k,label,x,y,a.px[y][x],b.px[y][x]);
            std::abort();
        }
}
static void variants(unsigned k){
    if(!k)return; // The exact captured intro is the first case.
    if(k<128){
        const int m=int(k%17)-8;
        w32(base+0x38,unsigned(m*7919));w32(base+0x3c,unsigned(m*-4133));
        w16(base+0x30,256+m*21);w16(base+0x32,unsigned(m*17));
        w16(base+0x34,unsigned(m*-13));w16(base+0x36,256+m*11);
        w16(base+0x10,(k*7)&511);w16(base+0x12,(k*3)&255);
        w16(base+0x14,(k*13)&511);w16(base+0x16,(k*11)&255);
        w16(base+0xe,(r16(base+0xe)&~0x2000u)|((k&1)?0x2000:0));
        const unsigned coefficients[]={0,1,4,8,12,16,17,31};
        w16(base+0x52,coefficients[k%8]|(coefficients[(k/8)%8]<<8));
        // Only affine BG3 and/or backdrop can be the immediate second target.
        w16(base+0x50,0x40|((k%4)&1?0x800:0)|((k%4)&2?0x2000:0));
        if(k>=64){
            for(unsigned i=16;i<32;++i){
                const unsigned a=oam+8*i;
                w16(a+2,(r16(a+2)&~511u)|((r16(a+2)+k*3)&511));
                w16(a+4,(r16(a+4)&~0xc00u)|((k%3)<<10));
            }
            if(k&4)w32(base,r32(base)&~0x300u); // transparent text/OBJ priority ties
            if(k&8)w32(base,r32(base)|0x80000000u); // extended OBJ palette input
        }
        return;
    }
    // Isolated overlaps retain real captured sprite texels and palette.
    const unsigned a0=r16(oam+16*8),a1=r16(oam+16*8+2),a2=r16(oam+16*8+4);
    for(unsigned i=0;i<128;++i)w16(oam+i*8,0x200);
    w32(base,r32(base)&~0x700u); // affine map plus OBJ
    for(unsigned i=0;i<2;++i){
        w16(oam+i*8,(a0&~0x1ffu)|((k&1)?175:31));
        w16(oam+i*8+2,(a1&~511u)|((k&2)?230:69));
        w16(oam+i*8+4,(a2&~0xc00u)|(((k/4+i)%3)<<10));
    }
    // Lower OAM index wins equal-priority overlap; normal/semi on either side.
    if(k&4)w16(oam,(r16(oam)&~0xc00u));
    if(k&8)w16(oam+8,(r16(oam+8)&~0xc00u));
    if((k&12)==12)w16(oam,r16(oam)|0x400); // always keep one semi OBJ
    if(k&16){
        // Affine OBJ, using a genuine signed fixed-point transform in OAM.
        w16(oam,r16(oam)|0x100|((k&32)?0x200:0));
        w16(oam+2,r16(oam+2)&~0x3e00u);
        w16(oam+6,256);w16(oam+14,64);w16(oam+22,unsigned(-48));w16(oam+30,224);
    }
    w16(base+0x50,0x40|((k&1)?0x800:0)|((k&2)?0x2000:0));
    w16(base+0x52,((k%4)*5)|((((k/4)%4)*7)<<8));
    if(k&32)std::memset(ntr::host_ptr(0x06200000),0,0x20000); // transparent affine map/backdrop
}
static void refused(unsigned k){
    switch(k){
    case 0:w32(base,r32(base)&~0x1000u);break;
    case 1:w32(base,r32(base)|0x80u);break;
    case 2:w32(base,r32(base)|0x2000u);break;
    case 3:w32(base,r32(base)|0x4000u);break;
    case 4:w32(base,r32(base)|0x8000u);break;
    case 5:w16(base+0x50,0x2800);break;
    case 6:w16(base+0x50,0x2880);break;
    case 7:w16(base+0x50,0x28c0);break;
    case 8:w16(base+0x50,0x2841);break;
    case 9:w16(base+0x50,0x2842);break;
    case 10:w16(base+0x50,0x2848);break;
    case 11:w16(base+0x50,0x2850);break;
    case 12:w16(base+0x50,0x3840);break;
    case 13:w16(base+0x50,0x2940);break;
    case 14:w16(base+0x50,0x2a40);break;
    case 15:w16(base+0x6c,0x4008);break;
    case 16:w16(oam+16*8,r16(oam+16*8)|0x1000);break;
    case 17:w16(oam+16*8,(r16(oam+16*8)&~0xc00u)|0x800);break;
    case 18:w16(oam+16*8,(r16(oam+16*8)&~0xc00u)|0xc00);break;
    case 19:w16(oam+16*8+4,(r16(oam+16*8+4)&~0xc00u)|0xc00);break;
    case 20:for(unsigned i=0;i<128;++i)w16(oam+i*8,r16(oam+i*8)&~0xc00u);break;
    case 21:w32(base,r32(base)&~0x30000u);break;
    case 22:w32(base,(r32(base)&~0x30000u)|0x20000u);break;
    case 23:w16(base+0xe,(r16(base+0xe)&~3u)|1u);break;
    default:std::abort();
    }
}
int main(int argc,char** argv){
    assert(argc==3);FILE* output=std::fopen(argv[2],"wb");assert(output);
    // Regression: two producers may both publish generation 1. The actual
    // consumer key must request an upload when the producer changes.
    uint32_t bg[1]={0xff000000},combined[1]={0xffffffff};
    render60::AffineOverlayKey key;assert(!key.matches(bg,1));key.commit(bg,1);
    assert(key.matches(bg,1));assert(!key.matches(combined,1));key.commit(combined,1);
    assert(key.matches(combined,1));assert(!key.matches(bg,1));assert(!key.matches(combined,2));
    key.reset();assert(!key.matches(combined,1));
    const unsigned sizes[]={0,0,0x400000,0,0x2000,0x800,0x900000,0x800};
    std::array<std::vector<uint8_t>,8> originals;
    for(unsigned i:{2,4,5,6,7}){
        ntr::g_region[i]=static_cast<uint8_t*>(std::calloc(1,0x1000000));assert(ntr::g_region[i]);
        if(i==2)continue;
        originals[i].resize(sizes[i]);char path[1024];std::snprintf(path,sizeof(path),"%s/region-%u.bin",argv[1],i);
        FILE* f=std::fopen(path,"rb");assert(f);assert(std::fread(originals[i].data(),1,sizes[i],f)==sizes[i]);std::fclose(f);
    }
    auto reset=[&]{for(unsigned i:{4,5,6,7})std::memcpy(ntr::g_region[i],originals[i].data(),sizes[i]);};
    unsigned admitted=0,rejected=0;double cpu_ms=0,overlay_ms=0;
    for(unsigned k=0;k<192+24;++k){
        reset();if(k<192)variants(k);else refused(k-192);
        const auto start=std::chrono::steady_clock::now();
        auto view=ntr::ppu_prepare_affine_alpha_objects(ntr::ENGINE_B);
        overlay_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        if(k>=192){if(view){std::fprintf(stderr,"unsupported case admitted: %u\n",k-192);std::abort();}++rejected;continue;}
        if(!view){std::fprintf(stderr,"supported case refused: %u\n",k);std::abort();}
        assert(view->gpu_affine && !view->state.has_geometry && !view->state.has_obj_alpha && !view->state.has_windows);
        rasterize(*view->gpu_affine);
        const auto cpu_start=std::chrono::steady_clock::now();
        auto original=ntr::ppu_prepare_candidates_cached(ntr::ENGINE_B);
        ntr::ppu_compose_candidates(original,geometry,reference);
        cpu_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cpu_start).count();
        equal(actual,reference,k,"cached-full-CPU");
        ntr::ppu_scanout_scene_with_geometry(ntr::ENGINE_B,scanout,geometry);
        equal(actual,scanout,k,"direct-full-CPU");
        assert(std::fwrite(&actual,sizeof(actual),1,output)==1);++admitted;
    }
    reset();assert(!ntr::ppu_prepare_affine_alpha_objects(ntr::ENGINE_A));++rejected;
    assert(std::fclose(output)==0);
    std::printf("{\"admitted\":%u,\"refused\":%u,\"pixels_compared_per_oracle\":%u,\"overlay_host_ms\":%.6f,\"full_cpu_host_ms\":%.6f}\n",admitted,rejected,admitted*ntr::SCREEN_W*ntr::SCREEN_H,overlay_ms,cpu_ms);
}
