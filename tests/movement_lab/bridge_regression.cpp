#include "controls.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include <array>
extern "C" void motion64_profile(float,float);
static unsigned voice_events;
static void sound(uint32_t bits,float*){if((bits>>28)==2)++voice_events;}
static SM64Surface floor_mesh[2]={
 {0,0,0,{{-20000,0,-20000},{20000,0,20000},{20000,0,-20000}}},
 {0,0,0,{{-20000,0,-20000},{-20000,0,20000},{20000,0,20000}}}};
static int spawn(){int id=sm64_mario_create(0,0,0);assert(id>=0);sm64_set_mario_action(id,0x0c400201);return id;}
static std::vector<std::array<float,7>> route(float speed,float jump){
 motion64_profile(speed,jump);int id=spawn();std::vector<std::array<float,7>> result;
 for(int i=0;i<240;++i){
  const bool moving=i>=20&&i<150;
  auto input=movement_lab::n64_input(moving?.4f:0,moving?.8f:0,0,i>=60&&i<75,false,i>=170&&i<190);
  SM64MarioState s{};sm64_mario_tick(id,&input,&s,nullptr);
  result.push_back({s.position[0],s.position[1],s.position[2],s.velocity[0],s.velocity[1],s.velocity[2],s.forwardVelocity});
 }
 sm64_mario_delete(id);return result;
}
int main(){
 sm64_global_init(nullptr,nullptr);sm64_register_play_sound_function(sound);sm64_static_surfaces_load(floor_mesh,2);
 const float directions[][2]={{0,1},{1,0},{0,-1},{-1,0},{.7071068f,.7071068f}};
 for(short camera:{short(0),short(0x4000),short(-0x4000),short(0x7fff)})for(auto& dir:directions){
  int id=spawn();SM64MarioState s{};auto input=movement_lab::n64_input(dir[0],dir[1],camera,false,false,false);
  for(int i=0;i<60;++i)sm64_mario_tick(id,&input,&s,nullptr);
  float expected=std::atan2(dir[0],-dir[1])+float(camera)*3.14159265358979323846f/32768.f;
  float difference=std::remainder(s.faceAngle-expected,2*3.14159265358979323846f);
  assert(std::fabs(difference)<.01f);sm64_mario_delete(id);
 }
 auto a=route(1,1),b=route(1,1);assert(a==b);
 assert(voice_events>0);
 auto wario=route(3072.f/3686.f,3358.f/4096.f);
 float mario_peak=0,wario_peak=0;for(auto& p:a)mario_peak=std::fmax(mario_peak,p[1]);for(auto& p:wario)wario_peak=std::fmax(wario_peak,p[1]);assert(wario_peak<mario_peak);
 assert(movement_lab::ds_animation(0x03000880,0)==0x53);
 assert(movement_lab::ds_animation(0x0c400201,0)==0x47);
 printf("20 real-controller direction cases; deterministic 240-tick reset replay; %u voice events; jump peaks %.3f / %.3f\n",voice_events,mario_peak,wario_peak);
 sm64_global_terminate();
}
