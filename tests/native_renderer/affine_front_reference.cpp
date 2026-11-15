// Authored register/tile inputs; full CPU PPU is the independent composition
// oracle. This validates publication/admission, not PICA sampling or hardware.
#include "ntr/ppu.h"
#include "ntr/mmio.h"
#include "ntr/vram_bus.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {volatile int sm64ds_gpu_affine_ready=0,sm64ds_gpu_affine_wanted=0;}
namespace ntr {
uint8_t* g_region[8]{};bool g_region_identity[8]{};
bool io_ready(){return true;}
uint64_t io_read(uint32_t a,unsigned size){uint64_t v=0;std::memcpy(&v,host_ptr(a),size);return v;}
void gx_render(Framebuffer&){std::abort();}
VramBanks* io_vram_banks(){return nullptr;}
}
static void half(unsigned a,unsigned v){auto h=uint16_t(v);std::memcpy(ntr::host_ptr(a),&h,2);}
static void word(unsigned a,unsigned v){std::memcpy(ntr::host_ptr(a),&v,4);}
int main(){
    const unsigned sizes[]={0,0,0x400000,0,0x2000,0x800,0x900000,0x800};
    for(unsigned i:{2,4,5,6,7}){ntr::g_region[i]=static_cast<uint8_t*>(std::calloc(1,sizes[i]));assert(ntr::g_region[i]);}
    constexpr unsigned regs=0x04001000,display=0x11b13;
    word(regs,display);half(regs+8,0x1800);half(regs+10,0x1901);half(regs+14,0x1f12);
    for(unsigned i=0;i<256;++i){half(0x05000400+i*2,(i*197+31)&32767);half(0x05000600+i*2,(i*311+157)&32767);}
    auto* chars=static_cast<uint8_t*>(ntr::host_ptr(0x06200000));
    for(unsigned i=0;i<32768;++i)chars[i]=i%5?uint8_t(i*13+i/8):0;
    auto* affine=static_cast<uint8_t*>(ntr::host_ptr(0x06210000));
    for(unsigned i=0;i<65536;++i)affine[i]=i%7?uint8_t(i*19+i/64):0;
    for(unsigned i=0;i<1024;++i){half(0x0620c000+i*2,((i%3)?(i%127+1):0)|((i&3)<<12));half(0x0620c800+i*2,((i%5)?0:(i%63+1)));}
    for(unsigned i=0;i<256;++i)half(0x0620f800+i*2,(i*7)%1024);
    auto* obj=static_cast<uint8_t*>(ntr::host_ptr(0x06600000));
    for(unsigned i=0;i<0x20000;++i)obj[i]=uint8_t(i%5?i*17+i/32:0);
    for(unsigned i=0;i<128;++i)half(0x07000400+i*8,0x200);
    for(unsigned i=8;i<16;++i){
        half(0x07000400+i*8,20+(i-8)*11|0x2000|((i&1)?0x400:0));
        half(0x07000402+i*8,(i-8)*27|0x4000);
        half(0x07000404+i*8,i*2|((i%3)<<10));
    }
    // One rotated sprite, alongside ordinary and semi-transparent sprites.
    half(0x07000440,0x2100|20);half(0x07000406,212);half(0x0700040e,83);
    half(0x07000416,unsigned(-83));half(0x0700041e,212);
    // Direct-colour bitmap OBJ carries its own alpha metadata. With no
    // second blend target its visible texels must remain opaque, as on DS.
    half(0x07000480,0x2c00|34);half(0x07000482,0x4000|62);half(0x07000484,0x8400|32);
    // Two alternating foreground buffers, as the runtime stores: each keeps
    // its own overlay base and restores only boxes its OBJs covered before.
    static ntr::Framebuffer buffers[2],reference,zero;unsigned cases=0;
    for(unsigned k=0;k<64;++k){
        const int move=int(k%16)-8,turn=int(k%7)-3;
        const unsigned blends[]={0,0x2840,0x0800,0x2841};
        half(regs+0x50,blends[k%4]);half(regs+0x52,k&4?0x0c04:0x0808);
        for(unsigned i=8;i<16;++i){half(0x07000404+i*8,i*2|((k%4==3?0:i%3)<<10));
            half(0x07000402+i*8,(((i-8)*27+k*13)&0x1ff)|0x4000);half(0x07000400+i*8,((20+(i-8)*11+k*5)&0xff)|0x2000|((i&1)?0x400:0));}
        half(0x07000484,0x8000|32|((k%4==3?0:1)<<10));
        half(regs+14,0x1f12|((k&16)?0x2000:0));
        word(regs+0x38,unsigned(move*431));word(regs+0x3c,unsigned(move*-719));
        half(regs+0x30,unsigned(256+move*3));half(regs+0x32,unsigned(turn*7));
        half(regs+0x34,unsigned(turn*-7));half(regs+0x36,unsigned(256+move*3));
        half(regs+0x10,k*3);half(regs+0x16,k*5);
        const auto original=ntr::ppu_prepare_candidates_cached(ntr::ENGINE_B);
        ntr::ppu_compose_candidates(original,zero,reference,false);
        auto& foreground=buffers[k&1];
        ntr::PpuGpuAffineLayer layer{};assert(ntr::ppu_prepare_native_affine(ntr::ENGINE_B,foreground,layer));
        assert(layer.raster&&layer.raster_generation&&!layer.overlay);
        for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x){
            int sx=(layer.ref_x+int(x)*layer.pa+int(y)*layer.pb)>>8;
            int sy=(layer.ref_y+int(x)*layer.pc+int(y)*layer.pd)>>8;
            if(layer.wrap){sx&=layer.width-1;sy&=layer.height-1;}
            uint32_t pixel=0;
            if(unsigned(sx)<unsigned(layer.width)&&unsigned(sy)<unsigned(layer.height))pixel=layer.raster[unsigned(sy)*layer.width+unsigned(sx)];
            if(!(pixel>>24))pixel=layer.backdrop;
            if(foreground.px[y][x]>>24)pixel=foreground.px[y][x];
            if(pixel!=reference.px[y][x]){std::fprintf(stderr,"case=%u pixel=%u,%u new=%08x original=%08x\n",k,x,y,pixel,reference.px[y][x]);std::abort();}
        }
        ++cases;
    }
    // Refusal checks use the original on-screen sprite positions.
    for(unsigned i=8;i<16;++i){half(0x07000400+i*8,20+(i-8)*11|0x2000|((i&1)?0x400:0));half(0x07000402+i*8,(i-8)*27|0x4000);}
    ntr::PpuGpuAffineLayer ignored{};
    word(regs,display|0x2000);assert(!ntr::ppu_prepare_native_affine(ntr::ENGINE_B,buffers[0],ignored));word(regs,display);
    half(regs+0x50,0x0080);assert(!ntr::ppu_prepare_native_affine(ntr::ENGINE_B,buffers[0],ignored));
    half(regs+0x50,0x0100);assert(!ntr::ppu_prepare_native_affine(ntr::ENGINE_B,buffers[0],ignored));half(regs+0x50,0);
    half(0x07000404+9*8,18|0xc00);assert(!ntr::ppu_prepare_native_affine(ntr::ENGINE_B,buffers[0],ignored));
    std::printf("PASS: %u authored native-affine publications, %u exact pixels, four unsupported admissions refused\n",cases,cases*256*192);
    for(unsigned i:{2,4,5,6,7})std::free(ntr::g_region[i]);
}
