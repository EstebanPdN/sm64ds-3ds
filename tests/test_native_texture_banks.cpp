// Persistent host contract harness. Upload-window table fixtures are generated
// by tools/test_native_texture_banks.py from the verified local ASMP0 pack;
// no ROM bytes belong in this source. Uses the real native VRAM/GX providers.
#include "sm64ds/game_geometry.hpp"
#include "sm64ds/native_video.hpp"
#include "sm64ds/native_platform.hpp"
#include "sm64ds/game_draw.hpp"
#include "ntr/gx.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <vector>
namespace GX {
void SetBankForTex(unsigned short);void SetBankForTexPltt(unsigned short);
void BeginLoadTex();void EndLoadTex();void BeginLoadTexPltt();void EndLoadTexPltt();
void LoadTex(const void*,unsigned,unsigned);void LoadTexPltt(const void*,unsigned,unsigned);
}
namespace {bool failed;struct ExpectedFailure {};unsigned failures;}
extern "C" {
std::uint16_t data_020a6088[13],data_02099fcc,data_020a6084;
std::uint8_t data_0208ee00;
std::uint32_t data_02099fd0=~0u,data_020a60a4,data_020a60a8;
std::uint32_t data_020a60ac,data_020a60b0,data_020a60b4,data_020a60b8,data_020a60bc,data_020a60c0;
void func_020190b8() {}
[[noreturn]] void Crash() {throw ExpectedFailure{};}
unsigned func_02053e34();unsigned func_02053e1c();
void Vram__Map(std::uint16_t mask) {
 const unsigned reg[]={0x240,0x241,0x242,0x243,0x244,0x245,0x246,0x248,0x249};
 for(unsigned bank=0;bank<9;++bank)if(mask&(1u<<bank))
  assert(sm64ds::native_video_write(0x04000000+reg[bank],0x80,1));
}
std::uint16_t DisableVramBanks(std::uint16_t* word) {
 const auto old=*word;*word=0;
 const unsigned reg[]={0x240,0x241,0x242,0x243,0x244,0x245,0x246,0x248,0x249};
 for(unsigned bank=0;bank<9;++bank)if(old&(1u<<bank))
  assert(sm64ds::native_video_write(0x04000000+reg[bank],0,1));
 return old;
}
extern std::uint16_t data_02086314[];
extern std::uint16_t data_02086324[];
extern std::uint16_t data_02086326[];
extern std::uint16_t data_02086328[];
}
namespace sm64ds {
void native_platform_fail(const char*) {failed=true;}
bool native_platform_ok() {return !failed;}
void native_platform_irq_set_master(bool) {}
void native_platform_irq_raise(std::uint32_t) {}
}
namespace {
unsigned read(unsigned address,unsigned size=4) {return unsigned(sm64ds::native_video_read(address,size));}
unsigned physical(unsigned bank) {return bank<4?0x06800000+bank*0x20000:bank==4?0x06880000:0x06890000+(bank-5)*0x4000;}
unsigned span(unsigned bank) {return bank<4?0x20000:bank==4?0x10000:0x4000;}
void reset() {
 failed=false;data_02099fd0=~0u;
 std::memset(data_020a6088,0,sizeof(data_020a6088));
 data_020a60ac=data_020a60b0=data_020a60b4=data_020a60b8=data_020a60bc=data_020a60c0=0;
 sm64ds::game_geometry()=sm64ds::GameGeometry{};
 sm64ds::native_video_reset();sm64ds_draw_reset();ntr::gx_reset();
 data_020a6088[0]=0x1ff;Vram__Map(0x1ff);
 for(unsigned bank=0;bank<7;++bank)
  std::memset(sm64ds::native_video_pointer(physical(bank),span(bank)),0x40+bank,span(bank));
}
template<class Func> void rejects(Func func) {
 try {func();assert(false);}catch(const ExpectedFailure&) {assert(failed);++failures;failed=false;}
}
std::vector<unsigned> selected(unsigned mask,bool palette) {
 std::vector<unsigned> list;
 for(unsigned bank=palette?4:0;bank<(palette?7u:4u);++bank)if(mask&(1u<<bank))list.push_back(bank);
 return list;
}
void compare(unsigned mask,bool palette) {
 auto& g=sm64ds::game_geometry();const auto* view=palette?g.palettes:g.textures;
 unsigned offset=0;
 for(auto bank:selected(mask,palette)) {
  assert(std::memcmp(view+offset,sm64ds::native_video_pointer(physical(bank),span(bank)),span(bank))==0);
  offset+=span(bank);
 }
 assert(!failed && sm64ds::native_video_ok());
}
void exercise(unsigned mask,bool palette) {
 reset();auto& g=sm64ds::game_geometry();auto banks=selected(mask,palette);
 if(palette)GX::SetBankForTexPltt(mask);else GX::SetBankForTex(mask);
 const unsigned capacity=palette?g.palette_capacity:g.texture_capacity;
 assert(data_020a6088[palette?5:4]==mask && (data_020a6088[0]&mask)==0);
 assert((palette?func_02053e1c():func_02053e34())==capacity);
 if(!palette)assert(read(0x04000060,2)&1);
 unsigned slot=0;
 for(auto bank:banks) {
  assert(read(0x04000240+bank,1)==(0x83|(slot<<3)));
  slot+=bank==4?2:1;
 }
 compare(mask,palette);
 if(palette)GX::BeginLoadTexPltt();else GX::BeginLoadTex();
 assert(data_020a6088[palette?5:4]==0 && (data_020a6088[0]&mask)==mask);
 assert((palette?func_02053e1c():func_02053e34())==0);
 assert((palette?data_020a60b4:data_020a60b8)==mask);
 for(auto bank:banks)assert(read(0x04000240+bank,1)==0x80);
 if(palette)assert(data_020a60b0==(unsigned(data_02086314[mask>>4])<<12));
 else {
  assert(data_020a60ac==(unsigned(data_02086324[mask*3])<<12));
  assert(data_020a60bc==(unsigned(data_02086326[mask*3])<<12));
  assert(data_020a60c0==(unsigned(data_02086328[mask*3])<<12));
 }
 const std::uint32_t words[]={0x01020304,0x11223344,0x55667788,0x99aabbcc,
                            0xddeeff00,0x12345678,0x87654321,0xfedcba98};
 if(palette)GX::LoadTexPltt(words,0x100,sizeof(words));else GX::LoadTex(words,0x100,sizeof(words));
 unsigned boundary=0;
 for(unsigned i=0;i+1<banks.size();++i) {
  boundary+=span(banks[i]);
  if(palette)GX::LoadTexPltt(words,boundary-16,sizeof(words));else GX::LoadTex(words,boundary-16,sizeof(words));
  assert(std::memcmp(sm64ds::native_video_pointer(physical(banks[i])+span(banks[i])-16,16),words,16)==0);
  assert(std::memcmp(sm64ds::native_video_pointer(physical(banks[i+1]),16),reinterpret_cast<const std::uint8_t*>(words)+16,16)==0);
 }
 // Complete original words only: a seven-byte copy leaves the last three untouched.
 if(palette)GX::LoadTexPltt(words,capacity-8,7);else GX::LoadTex(words,capacity-8,7);
 const auto last=banks.back();
 assert(read(physical(last)+span(last)-8)==words[0]);
 assert(read(physical(last)+span(last)-4)==((0x40+last)*0x01010101u));
 compare(mask,palette);
 rejects([&]{if(palette)GX::LoadTexPltt(words,capacity-4,8);else GX::LoadTex(words,capacity-4,8);});
 rejects([&]{if(palette)GX::BeginLoadTexPltt();else GX::BeginLoadTex();});
 if(palette)GX::EndLoadTexPltt();else GX::EndLoadTex();
 assert(data_020a6088[palette?5:4]==mask && !(data_020a6088[0]&mask));
 assert(!(palette?g.palette_transfer:g.texture_transfer));
 if(palette)assert(!data_020a60b4 && !data_020a60b0);
 else assert(!data_020a60b8 && !data_020a60ac && !data_020a60bc && !data_020a60c0);
 compare(mask,palette);
 // Remap a different physical bank: the logical view must follow real bytes.
 if(palette)GX::SetBankForTexPltt(0x40);else GX::SetBankForTex(8);
 compare(palette?0x40:8,palette);
 if(palette)GX::SetBankForTexPltt(0);else GX::SetBankForTex(0);
 assert(data_020a6088[palette?5:4]==0);
 assert((palette?func_02053e1c():func_02053e34())==0);
 if(!palette)assert(!(read(0x04000060,2)&1));
}
}
int main() {
 for(unsigned mask=1;mask<16;++mask)exercise(mask,false);
 const unsigned palettes[]={0x10,0x20,0x30,0x40,0x60,0x70};
 for(auto mask:palettes)exercise(mask,true);
 reset();GX::SetBankForTexPltt(0x30);
 const auto palette_before=read(0x04000244,4);
 rejects([]{GX::SetBankForTexPltt(0x50);});
 assert(data_020a6088[5]==0x30 && read(0x04000244,4)==palette_before);
 rejects([]{GX::SetBankForTex(0x10);});
 reset();rejects([]{GX::BeginLoadTex();});
 reset();GX::SetBankForTex(3);GX::BeginLoadTex();
 data_02099fd0=0;assert(sm64ds::native_video_write(0x040000b8,0x80000000,4));
 rejects([]{GX::EndLoadTex();});
 assert(sm64ds::game_geometry().texture_transfer && data_020a6088[4]==0 && data_020a60b8==3);
 std::printf("Host texture SDK contracts passed: 16 texture masks, 7 palette masks, physical/logical copies, split boundaries, %u rejection checks.\n",failures);
}
