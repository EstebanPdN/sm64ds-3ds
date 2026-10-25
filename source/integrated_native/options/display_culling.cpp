#include "display_culling.hpp"
#include "enhance_settings.h"
#include <cstring>
#include <cstdio>
extern "C" void __real__ZN7Clipper13Func_0201559CEv(void*);
extern "C" void __wrap__ZN7Clipper13Func_0201559CEv(void* self){
 // Verified Clipper layout: signed32aspect at0x4c. The original rebuild owns
 // all four plane calculations and normalization. Temporarily widen its input,
 // then restore the canonical aspect so repeated calls never compound it.
 if(!self)return;
 auto* field=static_cast<unsigned char*>(self)+0x4c;
 int32_t original;std::memcpy(&original,field,sizeof original);
 const auto mode=enhance::live().screen_mode.load(std::memory_order_relaxed);
 const int32_t adjusted=display_culling::aspect(original,mode);
 std::memcpy(field,&adjusted,sizeof adjusted);
 __real__ZN7Clipper13Func_0201559CEv(self);
 std::memcpy(field,&original,sizeof original);
 static int last=-1;
 if(last!=mode){last=mode;std::printf("Display culling mode=%u aspect=%ld planeAspect=%ld horizontalScale=%s nearFar=original verticalFov=original\n",unsigned(mode),long(original),long(adjusted),mode==1?"1.25":"1.0");}
}
