// PLAYABLE MODE diagnostic (opt-in SM64DS_DS_CENSUS=1): counts every access
// the game still makes to the emulated DS register file, by hardware block.
// Purpose: track the "100% native" goal; each non-zero block is a DS->3DS
// translation that must be replaced by a native implementation.
#ifdef SM64DS_FRESH_NATIVE_GAME
#include <cstdint>
#include <cstdio>
#include <cstdlib>
extern "C" void __real__ZN3ntr8io_writeEmyj(std::uint32_t,std::uint64_t,unsigned);
extern "C" std::uint64_t __real__ZN3ntr7io_readEmj(std::uint32_t,unsigned);
namespace {
enum Block {display2d_a,display2d_b,dma,timer,key,ipc,irq,other_io,vram_control,divider,root,power_3d,geometry,other,blocks};
const char* name[blocks]={"2D-A","2D-B","DMA","timer","key","IPC","IRQ","io-misc","VRAMCNT","divider","sqrt","power/3Ddisp","GX","other"};
std::uint64_t reads[blocks],writes[blocks];
int enabled=-1;
Block classify(std::uint32_t a){
    if(a>=0x04000000u&&a<0x04000070u)return display2d_a;
    if(a>=0x04001000u&&a<0x04001070u)return display2d_b;
    if(a>=0x040000b0u&&a<0x040000e0u)return dma;
    if(a>=0x04000100u&&a<0x04000110u)return timer;
    if(a>=0x04000130u&&a<0x04000138u)return key;
    if(a>=0x04000180u&&a<0x04000190u)return ipc;
    if(a>=0x04000208u&&a<0x04000220u)return irq;
    if(a>=0x04000130u&&a<0x04000240u)return other_io;
    if(a>=0x04000240u&&a<0x0400024au)return vram_control;
    if(a>=0x04000280u&&a<0x040002b0u)return divider;
    if(a>=0x040002b0u&&a<0x040002c0u)return root;
    if(a>=0x04000300u&&a<0x04000400u)return power_3d;
    if(a>=0x04000400u&&a<0x04000700u)return geometry;
    return other;
}
bool on(){if(enabled<0){const char* v=std::getenv("SM64DS_DS_CENSUS");enabled=v&&*v=='1';}return enabled==1;}
}
extern "C" void __wrap__ZN3ntr8io_writeEmyj(std::uint32_t a,std::uint64_t v,unsigned w){if(on())++writes[classify(a)];__real__ZN3ntr8io_writeEmyj(a,v,w);}
extern "C" std::uint64_t __wrap__ZN3ntr7io_readEmj(std::uint32_t a,unsigned w){if(on())++reads[classify(a)];return __real__ZN3ntr7io_readEmj(a,w);}
extern "C" void sm64ds_ds_census_report(std::uint64_t frame){
    if(!on())return;
    std::printf("DS census frame=%llu (reads/writes since last report):",(unsigned long long)frame);
    for(unsigned i=0;i<blocks;++i)if(reads[i]||writes[i])std::printf(" %s=%llu/%llu",name[i],(unsigned long long)reads[i],(unsigned long long)writes[i]);
    std::printf("\n");for(unsigned i=0;i<blocks;++i)reads[i]=writes[i]=0;
}
#endif
