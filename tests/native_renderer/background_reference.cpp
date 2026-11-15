// Isolated PRIVATE reference oracle. Original PPU is linked only into this
// host diagnostic, never the native renderer or Nintendo3DS laboratories.
#include "ntr/ppu.h"
#include "ntr/mmio.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
extern "C" {volatile int sm64ds_gpu_affine_ready=0,sm64ds_gpu_affine_wanted=0;}
namespace ntr {
std::uint8_t* g_region[8]{};bool g_region_identity[8]{};
bool io_ready(){return true;}
std::uint64_t io_read(std::uint32_t a,unsigned n){std::uint64_t v=0;std::memcpy(&v,host_ptr(a),n);return v;}
void gx_render(Framebuffer&){std::abort();}
}
namespace {
void read(const char* path,void* out,std::size_t bytes){FILE* f=std::fopen(path,"rb");assert(f);
    assert(std::fread(out,1,bytes,f)==bytes&&std::fgetc(f)==EOF&&std::fclose(f)==0);}
}
int main(int argc,char** argv){
    assert(argc==5||argc==7||argc==9);
    const auto characters=argc==9?unsigned(std::strtoul(argv[7],nullptr,10)):16384;
    const auto eight=argc==9?unsigned(std::strtoul(argv[8],nullptr,10)):0;
    assert(characters&&characters<=65536&&characters%32==0&&eight<=1);
    const std::size_t sizes[]={0,0,ntr::MAIN_SIZE,1024*1024,ntr::IO_SIZE,ntr::PLTT_SIZE,ntr::VRAM_SIZE,ntr::OAM_SIZE};
    for(unsigned i=2;i<8;++i){ntr::g_region[i]=static_cast<std::uint8_t*>(std::calloc(1,sizes[i]));assert(ntr::g_region[i]);}
    read(argv[1],ntr::host_ptr(0x06200000),characters);read(argv[2],ntr::host_ptr(0x05000400),512);read(argv[3],ntr::host_ptr(0x0620c000),2048);
    // Match the standalone native component's explicit authored black backdrop.
    *static_cast<std::uint16_t*>(ntr::host_ptr(0x05000400))=0;
    *static_cast<std::uint32_t*>(ntr::host_ptr(0x04001000))=0x10800;
    *static_cast<std::uint16_t*>(ntr::host_ptr(0x0400100e))=(24<<8)|(eight?128:0)|1;
    if(argc>=7){
        *static_cast<std::uint16_t*>(ntr::host_ptr(0x0400101c))=std::uint16_t(std::strtol(argv[5],nullptr,10));
        *static_cast<std::uint16_t*>(ntr::host_ptr(0x0400101e))=std::uint16_t(std::strtol(argv[6],nullptr,10));
    }
    static ntr::Framebuffer frame,empty;ntr::ppu_scanout_scene_with_geometry(ntr::ENGINE_B,frame,empty);
    FILE* out=std::fopen(argv[4],"wb");assert(out);
    for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x){const auto c=frame.px[y][x];
        const std::uint8_t rgb[]={std::uint8_t(c>>16),std::uint8_t(c>>8),std::uint8_t(c)};assert(std::fwrite(rgb,1,3,out)==3);
    }
    assert(std::fclose(out)==0);
    for(unsigned i=2;i<8;++i)std::free(ntr::g_region[i]);
    std::puts("PASS: original PPU isolated background; authored black backdrop, no GX render");
}
