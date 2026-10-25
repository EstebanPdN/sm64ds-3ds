// Compile in the actual derived PPU translation unit to exercise its local
// DISPCNT sampler and cache-key routing, without a ROM or GPU implementation.
#include "native_ppu.cpp"
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
int main(){
 std::array<uint8_t,0x10000> registers{};ntr::g_region[4]=registers.data();
 for(unsigned route:{0u,0x8000u})for(bool wanted:{false,true})for(bool ready:{false,true}){
  const uint16_t powcnt=uint16_t(route);std::memcpy(ntr::host_ptr(0x04000304),&powcnt,2);
  native_lower::wanted.store(wanted);native_lower::gpu_ready.store(ready);
  for(unsigned mask=0;mask<65536;++mask){
   const uint32_t original=0x01000000u|mask;
   for(unsigned address:{0x04000000u,0x04001000u}){
    std::memcpy(ntr::host_ptr(address),&original,4);
    const bool replace=address==0x04001000u && route && wanted && ready;
    assert(ntr::rd32(address)==(replace?original&~0x1300u:original));
    uint32_t unchanged=0;std::memcpy(&unchanged,ntr::host_ptr(address),4);
    assert(unchanged==original);
   }
  }
  ntr::g_window_cut_scope=false;
  assert(ntr::dispcnt_key_address(ntr::ENGINE_A)==0x04000000u);
  const bool masked=route && wanted && ready;
  assert((ntr::dispcnt_key_address(ntr::ENGINE_B)!=0x04001000u)==masked);
 }
 std::puts("Native UI: both display routes, readiness, 65536 DISPCNT masks and cache-key ownership passed");
}
