#include "libsm64.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>
int main(void) {
 struct SM64Surface floor[2]={
  {0,0,0,{{-8000,0,-8000},{8000,0,8000},{8000,0,-8000}}},
  {0,0,0,{{-8000,0,-8000},{-8000,0,8000},{8000,0,8000}}}
 };
 sm64_global_init(NULL,NULL);sm64_static_surfaces_load(floor,2);
 int id=sm64_mario_create(0,10,0);assert(id>=0);
 struct SM64MarioInputs input={0,1,0,0,0,0,0};
 struct SM64MarioState state={0};
 // Past ACT_START_SLEEPING/ACT_SLEEPING, wake-up previously looped forever
 // in stop_sound's uninitialized mixer bank (audio belongs to DS).
 for(int i=0;i<1800;i++)sm64_mario_tick(id,&input,&state,NULL);
 assert(fabsf(state.position[1])<1);
 input.stickY=1;
 for(int i=0;i<60;i++)sm64_mario_tick(id,&input,&state,NULL);
 assert(fabsf(state.forwardVelocity)>20);
 input.buttonA=1;float ground=state.position[1],peak=ground;
 for(int i=0;i<15;i++){sm64_mario_tick(id,&input,&state,NULL);if(state.position[1]>peak)peak=state.position[1];}
 assert(peak>ground+50);
 printf("N64 controller: walk %.3f jump rise %.3f action %08x\n",state.forwardVelocity,peak-ground,state.action);
 sm64_global_terminate();return 0;
}
