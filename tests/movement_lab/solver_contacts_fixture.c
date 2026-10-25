// Offline headless-engine fixture; never compiled into the production game.
#include "libsm64.h"
#include "solver_contacts.h"
#include "decomp/global_state.h"
#include "decomp/include/object_fields.h"
#include "obj_pool.h"
#include "decomp/game/platform_displacement.h"
struct MarioInstance {struct GlobalState* globalState;};
extern struct ObjPool s_mario_instance_pool;
extern struct SM64SurfaceCollisionData gWaterSurfacePseudoFloor;
struct GraphNodeRoot* gCurGraphNodeRoot;
struct GraphNodeMasterList* gCurGraphNodeMasterList;
struct GraphNodePerspective* gCurGraphNodeCamFrustum;
struct GraphNodeCamera* gCurGraphNodeCamera;
struct GraphNodeObject* gCurGraphNodeObject;
struct GraphNodeHeldObject* gCurGraphNodeHeldObject;
void* fixture_bound_state(void) {return g_state;}
extern void apply_platform_displacement(unsigned,struct SM64SurfaceObjectTransform*);
void fixture_core_platform_step(int id,struct SM64SurfaceObjectTransform* platform,
                                const float position[3],float output[3],short* yaw) {
    struct GlobalState* previous=g_state;
    struct MarioInstance* instance=s_mario_instance_pool.objects[id];g_state=instance->globalState;
    struct MarioState* state=&g_state->mgMarioStateVal;
    for(unsigned i=0;i<3;++i)state->pos[i]=position[i];state->faceAngle[1]=0;
    apply_platform_displacement(1,platform);
    for(unsigned i=0;i<3;++i)output[i]=state->pos[i];*yaw=state->faceAngle[1];
    g_state=previous;
}
void fixture_set_solver_contacts(int id,struct SM64SurfaceCollisionData** surfaces,
                                  struct SM64SurfaceObjectTransform* platform,int water) {
    struct MarioInstance* instance=s_mario_instance_pool.objects[id];
    struct MarioState* state=&instance->globalState->mgMarioStateVal;
    state->floor=water?&gWaterSurfacePseudoFloor:surfaces[0];
    state->ceil=surfaces[1];state->wall=surfaces[2];
    state->marioObj->oFloor=surfaces[3];state->marioObj->platform=platform;
}
