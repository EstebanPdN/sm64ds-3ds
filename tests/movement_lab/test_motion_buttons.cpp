#include "motion_buttons.hpp"
#include <cassert>
#include <cstdio>
using namespace native_motion;
int main(){
 // Actual DS Stage::CheckInput tables (data_0209214c), independently read
 // from the matching linked ELF. These layouts are separate from physics.
 constexpr unsigned table[3][16]={{1,2,4,8,16,32,64,128,1024,16384,32768,2048,0,0,0,0},
 {1,2,4,8,0,0,0,0,1024,16384,256,512,0,0,0,0},
 {1,1024,4,8,1,16384,2,1024,0,0,2,16384,0,0,0,0}};
 for(unsigned mode=0;mode<3;++mode)for(unsigned physical=0;physical<65536;++physical){
  unsigned logical=0;for(unsigned bit=0;bit<16;++bit)if(physical&(1u<<bit))logical|=table[mode][bit];
  unsigned expected=0;
  if(mode<2){if(physical&2)expected|=Jump;if(physical&1)expected|=Attack;if(physical&256)expected|=Crouch;}
  else{if(physical&((1u<<6)|(1u<<10)))expected|=Jump;if(physical&(1u|(1u<<4)))expected|=Attack;if(physical&(2u|(1u<<7)))expected|=Crouch;}
  assert(motion_buttons(uint16_t(logical))==expected);
 }
 for(unsigned physical=0;physical<4096;++physical)for(unsigned circle=0;circle<16;++circle){
  uint32_t held=physical|(circle<<28);
  assert(filter_circle_digital(held,true,true,false)==physical);
  assert(filter_circle_digital(held,true,true,true)==held);
  assert(filter_circle_digital(held,true,false,false)==held);
  assert(filter_circle_digital(held,false,true,false)==held);
 }
 assert(!motion_buttons(0x3bfcu));assert(motion_buttons(0x403)==(Jump|Attack|Crouch));
 std::puts("196608 DS mode/button combinations preserve logical jump/attack/crouch independently of physics");
}
