// Authored register/tile inputs; full CPU PPU is the independent composition
// oracle. This validates publication/admission, not PICA sampling or hardware.
#include "ntr/ppu.h"
#include "ntr/mmio.h"
#include "ntr/vram_bus.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
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
namespace ntr{extern unsigned ppu_affine_scroll_hits;}
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
    static ntr::Framebuffer buffers[2],reference,zero;unsigned cases=0,blended=0,worst=0;
    // BUILD140: a pixel with partial alpha is blended by the GPU over the
    // map (SRC_ALPHA, ONE_MINUS_SRC_ALPHA). The DS blends in 6 bits, the
    // PICA in 8: such pixels may differ from the CPU PPU by a few levels.
    constexpr unsigned kBlendTolerance=8;
    // BUILD141: GPU text layers. With a behind plane the PPU may hand BG0/BG1
    // to the GPU; the picture is then map, behind plane, layers back to
    // front, foreground. layered=true sets a scene they qualify for: both
    // overlays at priority 0, no bitmap OBJ, semi OBJs behind the layers.
    unsigned layered_cases=0;
    auto over=[&](uint32_t& pixel,uint32_t top,bool& partial){
        const unsigned a=top>>24;if(!a)return;
        if(a==255u){pixel=top;return;}
        uint32_t out=0xff000000u;
        for(unsigned shift=0;shift<24;shift+=8){const unsigned f=(top>>shift)&255u,b=(pixel>>shift)&255u;out|=((f*a+b*(255u-a)+127u)/255u)<<shift;}
        pixel=out;partial=true;
    };
    // k moves the sprites; base sets everything else (map, layers, blend).
    // scroll>=0 (BUILD142): BG0/BG1 offsets of that value and the publication
    // prepared twice before at the previous offset, with no reference pass in
    // between (it invalidates the memo): the scroll-only fast path's case.
    auto check=[&](unsigned k,ntr::Framebuffer& foreground,unsigned base,ntr::Framebuffer* behind=nullptr,bool layered=false,int scroll=-1){
        const int move=int(base%16)-8,turn=int(base%7)-3;
        const unsigned blends[]={0,0x2840,0x0800,layered?0x2842u:0x2841u};
        const bool fog=layered&&base%4==3; // BG1 (the backmost overlay) blended
        half(regs+0x50,blends[base%4]);half(regs+0x52,base&4?0x0c04:0x0808);
        half(regs+10,layered?0x1900:0x1901);
        for(unsigned i=8;i<16;++i){
            const unsigned priority=layered?(fog?0u:((i&1)?2u:i%3)):(base%4==3?0u:i%3);
            half(0x07000404+i*8,i*2|(priority<<10));
            half(0x07000402+i*8,(((i-8)*27+k*13)&0x1ff)|0x4000);
            half(0x07000400+i*8,((20+(i-8)*11+k*5)&0xff)|0x2000|((i&1)&&!fog?0x400:0));}
        if(layered)half(0x07000480,0x0200); // no bitmap OBJ
        else half(0x07000480,0x2c00|34);
        half(0x07000484,0x8000|32|((base%4==3?0:1)<<10));
        half(regs+14,0x1f12|((base&16)?0x2000:0));
        word(regs+0x38,unsigned(move*431));word(regs+0x3c,unsigned(move*-719));
        half(regs+0x30,unsigned(256+move*3));half(regs+0x32,unsigned(turn*7));
        half(regs+0x34,unsigned(turn*-7));half(regs+0x36,unsigned(256+move*3));
        auto offsets=[&](int s){half(regs+0x10,s<0?base*3:unsigned(s*3));half(regs+0x16,s<0?base*5:unsigned(s*5+1));half(regs+0x14,s<0?0u:unsigned(s*7));};
        ntr::PpuGpuAffineLayer layer{};
        if(scroll>=0){offsets(scroll-1);for(unsigned i=0;i<2;++i)assert(ntr::ppu_prepare_native_affine(ntr::ENGINE_B,foreground,layer,behind));}
        offsets(scroll);
        if(scroll>=0)assert(ntr::ppu_prepare_native_affine(ntr::ENGINE_B,foreground,layer,behind));
        const auto original=ntr::ppu_prepare_candidates_cached(ntr::ENGINE_B);
        ntr::ppu_compose_candidates(original,zero,reference,false);
        if(scroll<0)assert(ntr::ppu_prepare_native_affine(ntr::ENGINE_B,foreground,layer,behind));
        assert(layer.raster&&layer.raster_generation&&!layer.overlay);
        assert(!layer.text_count||behind);
        if(layer.text_count)++layered_cases;
        for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x){
            int sx=(layer.ref_x+int(x)*layer.pa+int(y)*layer.pb)>>8;
            int sy=(layer.ref_y+int(x)*layer.pc+int(y)*layer.pd)>>8;
            if(layer.wrap){sx&=layer.width-1;sy&=layer.height-1;}
            uint32_t pixel=0;bool partial=false;
            if(unsigned(sx)<unsigned(layer.width)&&unsigned(sy)<unsigned(layer.height))pixel=layer.raster[unsigned(sy)*layer.width+unsigned(sx)];
            if(!(pixel>>24))pixel=layer.backdrop;
            if(layer.text_count){
                over(pixel,behind->px[y][x],partial);
                for(unsigned t=0;t<layer.text_count;++t){const auto& text=layer.text[t];
                    over(pixel,text.raster[unsigned((int(y)+text.vofs)&(text.height-1))*unsigned(text.width)+unsigned((int(x)+text.hofs)&(text.width-1))],partial);}
            }
            over(pixel,foreground.px[y][x],partial);
            unsigned diff=0;
            for(unsigned shift=0;shift<24;shift+=8){const unsigned c=(pixel>>shift)&255u,r=(reference.px[y][x]>>shift)&255u;diff=std::max(diff,c>r?c-r:r-c);}
            if(partial){
                if(diff>kBlendTolerance){std::fprintf(stderr,"case=%u pixel=%u,%u blended=%08x original=%08x diff=%u layers=%u\n",k,x,y,pixel,reference.px[y][x],diff,unsigned(layer.text_count));std::abort();}
                ++blended;worst=std::max(worst,diff);continue;
            }
            if(diff){std::fprintf(stderr,"case=%u pixel=%u,%u new=%08x original=%08x layers=%u\n",k,x,y,pixel,reference.px[y][x],unsigned(layer.text_count));std::abort();}
        }
        ++cases;
    };
    // Two alternating foreground buffers, as the runtime stores: each keeps
    // its own overlay base and restores only boxes its OBJs covered before.
    for(unsigned k=0;k<64;++k)check(k,buffers[k&1],k);
    // BUILD140: a held image copies one buffer into the other (game_runtime
    // map hold). The copy must then prepare like a fresh buffer: without
    // ppu_native_affine_forget it restored only its own old OBJ boxes and
    // kept the other buffer's sprites as ghosts.
    // Same layers and map in both (the overlay base is reused), sprites
    // moved (k 40-47: every sprite on screen, so a ghost would show).
    // Both buffers prepared over that base, then buffer 0 copied into 1.
    for(unsigned k=40;k<46;k+=3){check(k,buffers[1],k);check(k+1,buffers[0],k);
        buffers[1]=buffers[0];ntr::ppu_native_affine_forget(buffers[1]);check(k+2,buffers[1],k);}
    // BUILD141: the same scenes with GPU text layers (two alternating stores,
    // each with its behind plane). Every base runs twice: a layer that just
    // changed stays on the CPU path once.
    static ntr::Framebuffer behind[2];
    for(unsigned base=0;base<32;++base)for(unsigned pass=0;pass<2;++pass){const unsigned k=base*2+pass;check(k,buffers[k&1],base,&behind[k&1],true);}
    // A held layered image: both planes copied, both forgotten.
    for(unsigned k=40;k<46;k+=3){check(k,buffers[1],k,&behind[1],true);check(k+1,buffers[0],k,&behind[0],true);
        buffers[1]=buffers[0];behind[1]=behind[0];ntr::ppu_native_affine_forget(buffers[1]);ntr::ppu_native_affine_forget(behind[1]);
        check(k+2,buffers[1],k,&behind[1],true);}
    // BUILD142: clouds scrolling over an unchanged scene (the opening): the
    // descriptor's offsets come from the scroll registers alone.
    {const unsigned before=ntr::ppu_affine_scroll_hits;
     for(unsigned step=1;step<9;++step)check(100+step,buffers[step&1],4+(step&1)*8,&behind[step&1],true,int(step));
     if(ntr::ppu_affine_scroll_hits==before){std::fprintf(stderr,"the scroll fast path never ran\n");std::abort();}
     std::printf("scroll fast path hits=%u\n",ntr::ppu_affine_scroll_hits-before);}
    // BUILD142: the held copy inherits the source buffer's state (clone)
    // instead of forgetting it: still no ghost, and no full redraw.
    for(unsigned k=40;k<46;k+=3){check(k,buffers[1],k);check(k+1,buffers[0],k);
        buffers[1]=buffers[0];ntr::ppu_native_affine_clone(buffers[1],buffers[0]);check(k+2,buffers[1],k);}
    for(unsigned k=40;k<46;k+=3){check(k,buffers[1],k,&behind[1],true);check(k+1,buffers[0],k,&behind[0],true);
        buffers[1]=buffers[0];behind[1]=behind[0];ntr::ppu_native_affine_clone(buffers[1],buffers[0]);ntr::ppu_native_affine_clone(behind[1],behind[0]);
        check(k+2,buffers[1],k,&behind[1],true);}
    // Without a behind plane the same scene never publishes layers.
    check(3,buffers[0],3,nullptr,true);check(3,buffers[1],3,nullptr,true);
    if(!layered_cases){std::fprintf(stderr,"no layered publication was exercised\n");std::abort();}
    // Refusal checks use the original on-screen sprite positions.
    for(unsigned i=8;i<16;++i){half(0x07000400+i*8,20+(i-8)*11|0x2000|((i&1)?0x400:0));half(0x07000402+i*8,(i-8)*27|0x4000);}
    ntr::PpuGpuAffineLayer ignored{};
    word(regs,display|0x2000);assert(!ntr::ppu_prepare_native_affine(ntr::ENGINE_B,buffers[0],ignored));word(regs,display);
    half(regs+0x50,0x0080);assert(!ntr::ppu_prepare_native_affine(ntr::ENGINE_B,buffers[0],ignored));
    half(regs+0x50,0x0100);assert(!ntr::ppu_prepare_native_affine(ntr::ENGINE_B,buffers[0],ignored));half(regs+0x50,0);
    half(0x07000404+9*8,18|0xc00);assert(!ntr::ppu_prepare_native_affine(ntr::ENGINE_B,buffers[0],ignored));
    std::printf("PASS: %u authored native-affine publications (%u with GPU text layers), %u pixels checked (%u exact, %u GPU-blended within %u/255, worst %u), four unsupported admissions refused\n",
        cases,layered_cases,cases*256*192,cases*256*192-blended,blended,kBlendTolerance,worst);
    for(unsigned i:{2,4,5,6,7})std::free(ntr::g_region[i]);
}
