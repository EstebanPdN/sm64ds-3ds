#include "libsm64.h"
#include "castle_surfaces.inc"
#include <stdio.h>
#include <assert.h>
#include <math.h>
int main(void){sm64_global_init(0,0);sm64_static_surfaces_load(castle_surfaces,sizeof(castle_surfaces)/sizeof(*castle_surfaces));float y=sm64_surface_find_floor_height(-1200,300,6800);printf("DS castle entrance N64 floor: %.4f (DS reference 254)\n",y);assert(fabsf(y-254)<2);sm64_global_terminate();}
