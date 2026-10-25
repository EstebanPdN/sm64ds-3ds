// Test-only bridge to independently executed original ARM collision queries.
// The fixture caches are not a production movement/surface-lifetime adapter.
#include "libsm64.h"
#include "collision_provider.h"
#include "castle_surfaces.inc"
#include <math.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>
// Rendering is intentionally absent in this CPU-only query executable. These
// are the same initial NULL graph cursors normally owned by the omitted renderer.
struct GraphNodeRoot* gCurGraphNodeRoot;
struct GraphNodeMasterList* gCurGraphNodeMasterList;
struct GraphNodePerspective* gCurGraphNodeCamFrustum;
struct GraphNodeCamera* gCurGraphNodeCamera;
struct GraphNodeObject* gCurGraphNodeObject;
struct GraphNodeHeldObject* gCurGraphNodeHeldObject;
struct SM64FloorCollisionData;
extern float find_floor_height(float,float,float);
extern float find_floor_height_and_data(float,float,float,struct SM64FloorCollisionData**);
typedef int (*DSQuery)(int32_t,int32_t,int32_t,int32_t*);
static DSQuery ds_query;
static struct SM64SurfaceCollisionData surfaces[32768];
static unsigned calls;
static int ds_floor(float x,float y,float z,float* height,struct SM64SurfaceCollisionData** out,void* context){
 (void)context;++calls;
 if(fabs((double)x)*4096>INT_MAX||fabs((double)y)*4096>INT_MAX||fabs((double)z)*4096>INT_MAX)return MOTION64_FLOOR_ERROR;
 int32_t hit[5]={0};
 if(!ds_query((int32_t)lroundf(x*4096),(int32_t)lroundf(y*4096),(int32_t)lroundf(z*4096),hit))return MOTION64_FLOOR_MISSING;
 if(hit[4]<=0||hit[4]>=32768)return MOTION64_FLOOR_ERROR;
 struct SM64SurfaceCollisionData* surface=&surfaces[hit[4]];
 memset(surface,0,sizeof *surface);surface->normal.x=hit[1]/4096.f;surface->normal.y=hit[2]/4096.f;surface->normal.z=hit[3]/4096.f;
 *height=hit[0]/4096.f;surface->originOffset=-(surface->normal.x*x+surface->normal.y*(*height)+surface->normal.z*z);surface->isValid=1;
 *out=surface;return MOTION64_FLOOR_HIT;
}
int oracle_init(void){sm64_global_init(NULL,NULL);sm64_static_surfaces_load(castle_surfaces,sizeof castle_surfaces/sizeof *castle_surfaces);return 1;}
void oracle_provider(DSQuery fn){ds_query=fn;calls=0;motion64_set_floor_provider(fn?ds_floor:NULL,NULL);}
unsigned oracle_calls(void){return calls;}
unsigned oracle_faults(void){return motion64_floor_provider_faults();}
int oracle_floor(float x,float y,float z,float* out){struct SM64SurfaceCollisionData* floor=NULL;float h=sm64_surface_find_floor(x,y,z,&floor);if(!floor)return 0;out[0]=h;out[1]=floor->normal.x;out[2]=floor->normal.y;out[3]=floor->normal.z;return 1;}
float oracle_height(float x,float y,float z){return find_floor_height(x,y,z);}
float oracle_details(float x,float y,float z,int* valid){struct SM64FloorCollisionData* floor=NULL;float h=find_floor_height_and_data(x,y,z,&floor);*valid=floor!=NULL;return h;}
void oracle_shutdown(void){motion64_set_floor_provider(NULL,NULL);ds_query=NULL;sm64_global_terminate();}
