#include "libsm64.h"
#include "decomp/include/types.h"
extern int mario_get_floor_class(struct MarioState*);
extern int mario_floor_is_slope(struct MarioState*);
int fixture_material_core(int type,int terrain,int normal_y,int slope) {
    struct SM64SurfaceCollisionData floor={0};floor.type=type;floor.normal.y=normal_y/4096.f;
    struct MarioState state={0};state.floor=&floor;state.curTerrain=terrain;
    return slope?mario_floor_is_slope(&state):mario_get_floor_class(&state);
}
