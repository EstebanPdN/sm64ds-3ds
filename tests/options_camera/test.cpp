#include "cstick_camera.h"
#include "analog_pad.h"
#include "enhance_settings.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <initializer_list>

extern "C" {
alignas(8) unsigned char data_0209f498[4*0x18]{};
unsigned char data_0209f2d8=1,data_0209d660=0,data_0209f21c=1,data_020a0e40=0;
unsigned char data_0209f294=0,data_0209f2c4=0,data_0209f20c=0,data_ov002_02111180=0;
int data_0209caa0[4]{},data_0209fc48=0;
unsigned data_0209b454=0;
alignas(8) unsigned char camera[0x200]{};
void* data_0209f318=camera;
char _ZTV6dMap_c[32]{};
void __real__ZN5Stage10CheckInputEv(void*){}
int __real__ZN7fBase_c14BeforeBehaviorEv(void*){return 1;}
void __real__ZN7fBase_c13AfterBehaviorEj(void*,unsigned){}
short _ZN4cstd5atan2E5Fix12IiES1_(int y,int x){return short(std::atan2(double(y),double(x))*32768/3.141592653589793);}
}
extern "C" void __wrap__ZN5Stage10CheckInputEv(void*);
void frame(int x,int y){sm64ds_cstick_inject(1,x,y);cstick_camera::after_check_input();}
void reset(){cstick_camera::set_mode(cstick_camera::Mode::Off);frame(0,0);cstick_camera::set_mode(cstick_camera::Mode::Free);frame(0,0);}
int main(){
unsigned flags=0x1000;std::memcpy(camera+0x154,&flags,4);
unsigned checks=0;
for(int sensitivity:{25,50,100,200}){
 enhance::live().camera_sensitivity=sensitivity;
 for(int x=0;x<=180;++x){assert(cstick_camera::free_target_speed(x)==-cstick_camera::free_target_speed(-x));++checks;}
}
enhance::live().camera_sensitivity=100;
for(int n=1;n<90;++n){
 reset();short right=0;for(int i=0;i<n;++i){frame(104,0);assert(sm64ds_cstick_cam_follow(&right,camera));assert(data_0209f498[4]==0&&data_0209f498[5]==0&&data_0209f498[6]==0&&data_0209f498[7]==0);}
 reset();short left=0;for(int i=0;i<n;++i){frame(-104,0);assert(sm64ds_cstick_cam_follow(&left,camera));} assert(short(-right)==left);++checks;
 reset();frame(0,104);int up=sm64ds_cstick_cam_pitch(4096,camera);
 reset();frame(0,-104);assert(sm64ds_cstick_cam_pitch(4096,camera)==-up);++checks;
}
reset();frame(16,16);short yaw=0;assert(!sm64ds_cstick_cam_follow(&yaw,camera));
frame(104,104);enhance::live().overlay_open=true;sm64ds_cstick_inject(0,0,0);cstick_camera::after_check_input();assert(!sm64ds_cstick_cam_follow(&yaw,camera));enhance::live().overlay_open=false;
reset();frame(104,104);data_0209f2d8=2;frame(104,104);assert(!sm64ds_cstick_cam_follow(&yaw,camera));data_0209f2d8=1;
reset();frame(104,104);data_0209f498[5]=0x40;frame(0,0);assert(!sm64ds_cstick_cam_follow(&yaw,camera));assert(sm64ds_cstick_cam_pitch(4096,camera)==0);data_0209f498[5]=0;
for(int sensitivity:{25,100,200})for(int threshold:{25,70,100}){
 enhance::live().move_sensitivity=sensitivity;enhance::live().run_threshold=threshold;
 int last=0;for(int x=0;x<=180;++x){auto r=analog_pad::map(x,0),l=analog_pad::map(-x,0),u=analog_pad::map(0,x);assert(r.mag>=last);assert(r.mag==l.mag&&r.mag==u.mag);assert(r.nx==-l.nx&&r.nx==-u.ny);last=r.mag;++checks;}
}
// Compare actual angular displacement reconstructed from the orbit vector,
// not numeric settings or a linear height fraction. Same deflection/sensitivity
// must trace the same angle on each axis at different physical distances.
for(int radius : {4096,917504,16777216})for(int sensitivity : {25,50,100,200})for(int raw : {-104,-72,-48,-24,24,48,72,104}){
 enhance::live().camera_sensitivity=sensitivity;
 // Deliberately mismatched legacy field: one user-facing camera sensitivity
 // governs both axes, so old INI pitch values cannot silently skew the rate.
 enhance::live().camera_pitch_sensitivity=25;
 reset();short angle=0;int vertical=0,horizontal=radius;
 for(int n=0;n<12;++n){
  // Free yaw follows the stick with the corrected (inverted) X sign, so the
  // same angular rate on both axes needs opposite raw X.
  frame(-raw,raw);assert(sm64ds_cstick_cam_follow(&angle,camera));
  sm64ds_cstick_cam_orbit(radius,&vertical,&horizontal,camera);
  if(std::abs(int(angle))>=cstick_camera::kMaxPitch)break;
  double measured=std::atan2(double(vertical),double(horizontal))*65536/6.283185307179586;
  assert(std::fabs(measured-angle)<4.0);
  assert(std::fabs(std::hypot(double(vertical),double(horizontal))-radius)<2.0);
  assert(sm64ds_cstick_cam_height(radius,radius/3,camera)==vertical);
  assert(sm64ds_cstick_cam_direct(radius,vertical,camera));
  ++checks;
 }
}
// The mode/ground/ceiling bound feeds back into the angle: holding the stick
// against a bound cannot accumulate an invisible target beyond that bound.
reset();frame(104,104);int vertical=0,horizontal=1000000;
sm64ds_cstick_cam_orbit(1000000,&vertical,&horizontal,camera);
assert(sm64ds_cstick_cam_direct(1000000,0,camera));
assert(std::abs(sm64ds_cstick_cam_height(1000000,0,camera))<2);
// New zone/camera invalidates accumulated tilt; no stale state in scenes.
reset();frame(104,104);void* zone=reinterpret_cast<void*>(0x1234);std::memcpy(camera+0x13c,&zone,sizeof zone);frame(0,0);assert(!sm64ds_cstick_cam_follow(&yaw,camera));
analog_pad::set_enabled(true);sm64ds_analog_pad_inject(1,140,0,0);
for(int mode=0;mode<3;++mode){
 std::memset(data_0209f498,0,0x18);data_0209f498[4]=0x55;data_0209f498[6]=0xaa;
 enhance::live().controller_mode=mode;const auto before=sm64ds_analog_pad_applied();
 __wrap__ZN5Stage10CheckInputEv(nullptr);
 assert(data_0209f498[0x16]==mode&&sm64ds_analog_pad_applied()==before+1);
 int16_t mag=0;std::memcpy(&mag,data_0209f498+8,2);assert(mag==0x1000&&data_0209f498[0x14]==1);
 assert(data_0209f498[4]==0x55&&data_0209f498[6]==0xaa); // button maps remain retail
 data_0209f498[8]=0x34;const auto touched=sm64ds_analog_pad_applied();
 __wrap__ZN5Stage10CheckInputEv(nullptr);assert(sm64ds_analog_pad_applied()==touched&&data_0209f498[8]==0x34);
}
sm64ds_analog_pad_inject(0,0,0,0);
std::printf("camera/analog checks=%u; symmetric yaw/pitch, no DS bits, gates, movement curve passed\n",checks);
}
