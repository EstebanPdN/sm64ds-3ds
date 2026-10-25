// Actual captured PPU bytes, differential native-ui masking and cache toggles.
#include "ntr/ppu.h"
#include "ntr/mmio.h"
#include "ntr/vram_bus.h"
#include "platform/3ds/native_lower_state.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {volatile int sm64ds_gpu_affine_ready=0,sm64ds_gpu_affine_wanted=0;}
namespace ntr {
uint8_t* g_region[8]{};bool g_region_identity[8]{};
bool io_ready(){return true;}
uint64_t io_read(uint32_t a,unsigned n){uint64_t v=0;std::memcpy(&v,host_ptr(a),n);return v;}
void gx_render(Framebuffer&){std::abort();}
VramBanks* io_vram_banks(){return nullptr;}
}
static ntr::Framebuffer geometry{},output{};
int main(int argc,char** argv){
 assert(argc==4);const bool production=std::atoi(argv[3])!=0;
 FILE* f=std::fopen(argv[2],"wb");assert(f);
 const unsigned sizes[]={0,0,0x400000,0,0x2000,0x800,0x900000,0x800};
 for(unsigned i:{2,4,5,6,7}){
  ntr::g_region[i]=static_cast<uint8_t*>(std::calloc(1,0x1000000));assert(ntr::g_region[i]);if(i==2)continue;
  char path[1024];std::snprintf(path,sizeof path,"%s/region-%u.bin",argv[1],i);FILE* source=std::fopen(path,"rb");assert(source);
  assert(std::fread(ntr::g_region[i],1,sizes[i],source)==sizes[i]);assert(std::fclose(source)==0);
 }
 std::array<uint8_t,0x2000> original;std::memcpy(original.data(),ntr::g_region[4],original.size());
 for(unsigned step=0;step<48;++step){
  std::memcpy(ntr::g_region[4],original.data(),original.size());
  // Both physical display routes: engine B is upper in the retail title,
  // lower during Stage pause. A native lower image must never hide upper OBJ.
  const bool a_upper=step>=24;
  uint16_t routing;std::memcpy(&routing,ntr::host_ptr(0x04000304),2);
  routing=uint16_t((routing&~0x8000u)|(a_upper?0x8000u:0));
  std::memcpy(ntr::host_ptr(0x04000304),&routing,2);
  const bool on=(step%3)==1;
  native_lower::wanted.store(production&&on);native_lower::gpu_ready.store(production);
  if(on&&!production&&a_upper){uint32_t value;std::memcpy(&value,ntr::host_ptr(0x04001000),4);value&=~0x1300u;std::memcpy(ntr::host_ptr(0x04001000),&value,4);}
  // Scroll the genuine map, then hold identical registers across off/on/off.
  const int scroll=int(step/3)*61-122;std::memcpy(ntr::host_ptr(0x04001038),&scroll,4);
  uint32_t before;std::memcpy(&before,ntr::host_ptr(0x04001000),4);
  for(auto eng:{ntr::ENGINE_A,ntr::ENGINE_B}){
   const auto view=ntr::ppu_prepare_candidates_cached(eng);
   ntr::ppu_compose_candidates(view,geometry,output);
   assert(std::fwrite(&output,sizeof output,1,f)==1);
  }
  uint32_t after;std::memcpy(&after,ntr::host_ptr(0x04001000),4);assert(after==before);
 }
 assert(std::fclose(f)==0);
 for(unsigned i:{2,4,5,6,7})std::free(ntr::g_region[i]);
}
