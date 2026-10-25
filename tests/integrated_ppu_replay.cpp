// Offline differential test: only the captured 2D memory/registers are loaded.
// No actor, physics, save or live emulator state is manufactured by this test.
#include "ntr/ppu.h"
#include "ntr/mmio.h"
#include "ntr/vram_bus.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
extern "C" {volatile int sm64ds_gpu_affine_ready=1,sm64ds_gpu_affine_wanted=0;}
namespace ntr {
uint8_t* g_region[8]{};bool g_region_identity[8]{};
bool io_ready(){return true;}
uint64_t io_read(uint32_t address,unsigned size){uint64_t value=0;std::memcpy(&value,host_ptr(address),size);return value;}
void gx_render(Framebuffer&){std::abort();}
VramBanks* io_vram_banks(){return nullptr;}
}
static void w16(unsigned address,unsigned value){auto v=uint16_t(value);std::memcpy(ntr::host_ptr(address),&v,2);}
static void w32(unsigned address,unsigned value){std::memcpy(ntr::host_ptr(address),&value,4);}
int main(int argc,char** argv){
    assert(argc==3);FILE* output=std::fopen(argv[2],"wb");assert(output);
    const unsigned sizes[]={0,0,0x400000,0,0x2000,0x800,0x900000,0x800};
    for(unsigned i:{2,4,5,6,7}){
        ntr::g_region[i]=static_cast<uint8_t*>(std::calloc(1,0x1000000));assert(ntr::g_region[i]);
        if(i==2)continue;
        char path[1024];std::snprintf(path,sizeof(path),"%s/region-%u.bin",argv[1],i);
        FILE* f=std::fopen(path,"rb");assert(f);assert(std::fread(ntr::g_region[i],1,sizes[i],f)==sizes[i]);std::fclose(f);
    }
    std::array<uint8_t,0x2000> registers;std::memcpy(registers.data(),ntr::g_region[4],registers.size());
    uint64_t prior_generation=0;unsigned changes=0,admitted=0;double elapsed_ms=0;
    for(unsigned k=0;k<64;++k){
        std::memcpy(ntr::g_region[4],registers.data(),registers.size());
        const unsigned base=0x04001000;
        const unsigned modes[]={0x2840,0x2841,0x2842,0x2860};w16(base+0x50,modes[k/16]);
        if(k>=16 && k<48 && (k&8)){
            unsigned display;std::memcpy(&display,ntr::host_ptr(base),4);w32(base,display&~(1u<<12));
        }
        // Vary affine camera and cloud scroll independently, including held inputs.
        const int movement=int(k%16)-8;w32(base+0x38,unsigned(movement*431));w32(base+0x3c,unsigned(movement*-719));
        w16(base+0x30,256+movement*3);w16(base+0x32,unsigned(movement*7));
        w16(base+0x34,unsigned(movement*-7));w16(base+0x36,256+movement*3);
        w16(base+0x10,((k%16)/4)*3);w16(base+0x16,((k%16)/8)*5);
        const auto start=std::chrono::steady_clock::now();auto view=ntr::ppu_prepare_background_affine(ntr::ENGINE_B);
        elapsed_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        const uint32_t valid=bool(view);assert(std::fwrite(&valid,4,1,output)==1);if(!view)continue;
        ++admitted;const auto* layer=view->gpu_affine;assert(layer);
        if(layer->overlay_generation!=prior_generation)++changes;prior_generation=layer->overlay_generation;
        const int32_t values[]={layer->width,layer->height,layer->ref_x,layer->ref_y,layer->pa,layer->pb,layer->pc,layer->pd,
            layer->wrap,int32_t(layer->backdrop),layer->layer,layer->priority,layer->effect_mode,layer->effect_factor,
            layer->effect_layer,layer->effect_backdrop,view->state.blend,view->state.alpha,bool(layer->overlay)};
        assert(std::fwrite(values,sizeof(values),1,output)==1);
        assert(std::fwrite(layer->raster,4,size_t(layer->width)*layer->height,output)==size_t(layer->width)*layer->height);
        if(layer->overlay)assert(std::fwrite(layer->overlay,4,ntr::SCREEN_W*ntr::SCREEN_H,output)==ntr::SCREEN_W*ntr::SCREEN_H);
    }
    assert(std::fclose(output)==0);assert(admitted);
    std::printf("cases=64 admitted=%u overlayChanges=%u preparationHostMs=%.6f\n",admitted,changes,elapsed_ms);
}
