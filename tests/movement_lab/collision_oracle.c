#include "libsm64.h"
#include "castle_surfaces.inc"
int oracle_init(void){sm64_global_init(0,0);sm64_static_surfaces_load(castle_surfaces,sizeof(castle_surfaces)/sizeof(*castle_surfaces));return 1;}
int oracle_floor(float x,float y,float z,float* out){struct SM64SurfaceCollisionData* floor=0;float height=sm64_surface_find_floor(x,y,z,&floor);if(!floor)return 0;out[0]=height;out[1]=floor->normal.x;out[2]=floor->normal.y;out[3]=floor->normal.z;return 1;}
