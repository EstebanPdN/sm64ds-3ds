#include "libsm64.h"
#include "castle_surfaces.inc"
#include <cstdio>
#include <cstdlib>
extern "C" void motion64_tick_circle(int,int,int,short,int,int,int,SM64MarioState*);
extern "C" void motion64_detail(unsigned*,unsigned*);
int main(){
 sm64_global_init(nullptr,nullptr);sm64_static_surfaces_load(castle_surfaces,sizeof(castle_surfaces)/sizeof(*castle_surfaces));
 int id=-1;char line[2048];
 while(std::fgets(line,sizeof line,stdin)){
  unsigned tick,jump,attack,crouch;int x,y,camera,water;float px,py,pz,yaw,forward,vy;unsigned air;
  if(std::sscanf(line,"RESET %f %f %f %f %f %f %u",&px,&py,&pz,&yaw,&forward,&vy,&air)==7){
   if(id>=0)sm64_mario_delete(id);id=sm64_mario_create(px,py+2,pz);if(id<0)return 2;
   sm64_set_mario_position(id,px,py,pz);sm64_set_mario_faceangle(id,yaw);sm64_set_mario_forward_velocity(id,forward);sm64_set_mario_velocity(id,0,vy,0);sm64_set_mario_action(id,air?0x0100088c:0x0c400201);
  }else if(std::sscanf(line,"TICK %u %d %d %d %u %u %u %d",&tick,&x,&y,&camera,&jump,&attack,&crouch,&water)==8){
   sm64_set_mario_water_level(id,water);SM64MarioState s{};motion64_tick_circle(id,x,y,short(camera),jump,attack,crouch,&s);unsigned timer,phase;motion64_detail(&timer,&phase);
   std::printf("%u %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %u %u %u %d %d\n",tick,s.position[0],s.position[1],s.position[2],s.velocity[0],s.velocity[1],s.velocity[2],s.forwardVelocity,s.faceAngle,s.action,timer,phase,s.animID,s.animFrame);
  }
 }
 sm64_global_terminate();
}
