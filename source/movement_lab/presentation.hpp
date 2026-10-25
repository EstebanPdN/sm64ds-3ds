#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace movement_lab {
struct Presentation { unsigned animation,flags; int rate; bool footsteps; };
// DS assets and playback rates: func_ov002_020d4748, St_Wait_Init,
// St_TurnAround_Init/Main and the individual DS jump initializers.
// Locomotion selection follows the imported controller's animation state,
// including its 22/18 speed hysteresis, rather than the DS input globals.
inline Presentation presentation(uint32_t action,int n64_anim,float velocity) {
 const float speed=std::fabs(velocity);
 constexpr unsigned once=0x40000000;
 switch(action){
 case 0x04000440: case 0x0400044a:
  if(speed<.5f)return {0x47,0,0x1000,false};
  if(n64_anim==0x72)return {0x3f,0,0x2800,true};
  return {0x48,0,std::max(0x400,int(speed*512)),true};
 case 0x00000443: case 0x04000445:return {0x46,once,0x1000,false};
 case 0x00000444:return {0x45,once,0x1000,false};
 case 0x03000880:return {0x53,once,0x1000,false};
 case 0x03000881:return {0x4f,once,0x1000,false};
 case 0x01000882:return {0x4b,once,0x1000,false};
 case 0x01000883:return {0x2a,once,0x1000,false};
 case 0x03000886:return {0x28,once,0x1000,false};
 case 0x01000887:return {0x4d,once,0x1000,false};
 case 0x03000888:return {0x1a,once,0x1000,false};
 case 0x008008a9:return {0x3d,once,0x1000,false};
 case 0x0c008220:return {0x2c,0,0x1000,false};
 case 0x0c008221:return {0x2d,once,0x1000,false};
 case 0x0c008222:return {0x2e,once,0x1000,false};
 case 0x04008448:return {0x64,0,std::max(0x400,int(speed*512)),false};
 default:return {(action&0x800)?0x54u:0x47u,(action&0x800)?once:0u,0x1000,false};
 }
}
// Only verified voice families. The original DS helper selects each jump's
// two random variants from data_ov002_020ff284, using the live DS RNG.
inline int jump_voice_group(unsigned id,uint32_t action){
 if(action==0x03000886)return -1; // wall kick uses DS voice 0x0c
 if(action==0x03000888)return -1; // long jump uses DS voice 4
 if(id<=2)return 0;
 if(id==3)return action==0x03000881?1:0;
 if(id==4 || (id>=0x2b && id<=0x2f))return 2;
 return -1;
}
}
