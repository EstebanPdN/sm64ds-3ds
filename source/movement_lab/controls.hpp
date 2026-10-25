#pragma once
#include <cmath>
#include "libsm64.h"
namespace movement_lab {
inline SM64MarioInputs n64_input(float x,float y,short camera,bool jump,bool attack,bool crouch) {
 const float length=std::sqrt(x*x+y*y);
 if(length<.10f)x=y=0;
 else if(length>1.f){x/=length;y/=length;}
 const float yaw=float(camera)*3.14159265358979323846f/32768.f;
 // DS atan2(x,-y) and N64 atan2s(-y,x) have different argument
 // conventions. libsm64 additionally negates its public stickX input.
 // Reverse that API convention so right means +X at camera yaw zero.
 return {std::sin(yaw),std::cos(yaw),-x,y,
         uint8_t(jump),uint8_t(attack),uint8_t(crouch)};
}
inline int ds_animation(uint32_t action,float speed) {
 switch(action){
 case 0x03000880:return 0x53; // jump
 case 0x03000881:return 0x4f; // double jump
 case 0x01000882:return 0x4b; // triple jump
 case 0x01000883:return 0x2a; // backflip
 case 0x01000887:return 0x4d; // side flip
 case 0x03000888:return 0x1a; // long jump
 case 0x008008a9:return 0x3d; // ground pound
 case 0x0c008220:return 0x2c; // crouch
 case 0x04008448:return 0x64; // crawl
 default:return (action&0x800)?0x54:std::fabs(speed)>.5f?0x48:0x47;
 }
}
}
