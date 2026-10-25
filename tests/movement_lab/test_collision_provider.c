#include "collision_provider.h"
#include "libsm64.h"
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <string.h>
struct Context {struct SM64SurfaceCollisionData floor;unsigned calls;int result;};
static int provider(float x,float y,float z,float* height,struct SM64SurfaceCollisionData** out,void* opaque){
 struct Context* c=opaque;++c->calls;assert(x==1.25f&&y==254.5f&&z==-3.75f);*height=254.125f;*out=&c->floor;return c->result;
}
struct WallContext {struct SM64SurfaceCollisionData wall;int mode;unsigned calls;};
struct PlatformContext {int mode;unsigned calls;};
static int platform_provider(const struct SM64SurfaceObjectTransform* handle,float position[3],int16_t* yaw,void* opaque){
 struct PlatformContext* c=opaque;assert(handle);++c->calls;
 position[0]+=10;position[1]+=20;position[2]-=30;*yaw=16384;
 if(c->mode==1)position[1]=NAN;
 if(c->mode==2)return 0;
 if(c->mode==3)return -1;
 if(c->mode==4)return 2;
 return 1;
}
static int wall_provider(struct SM64WallCollisionData* data,void* opaque){
 struct WallContext* c=opaque;++c->calls;
 if(c->mode==4)return 0;
 data->x+=1;data->numWalls=1;data->walls[0]=&c->wall;
 if(c->mode==1)data->x=NAN;
 if(c->mode==2)data->y+=1;
 if(c->mode==3)return -1;
 if(c->mode==5)return 0;
 if(c->mode==6)data->numWalls=5;
 return 1;
}
int main(void){
 float height=123;struct SM64SurfaceCollisionData sentinel;struct SM64SurfaceCollisionData* floor=&sentinel;
 assert(!motion64_query_floor(1,2,3,&height,&floor)&&height==123&&floor==&sentinel);
 struct Context c;memset(&c,0,sizeof c);c.floor.normal.y=1;c.floor.isValid=1;c.result=MOTION64_FLOOR_HIT;
 motion64_set_floor_provider(provider,&c);
 assert(motion64_query_floor(1.25f,254.5f,-3.75f,&height,&floor));assert(height==254.125f&&floor==&c.floor&&c.calls==1);
 c.result=MOTION64_FLOOR_MISSING;assert(motion64_query_floor(1.25f,254.5f,-3.75f,&height,&floor)&&!floor&&height==-11000.f);
 assert(!motion64_floor_provider_faults());
 c.result=MOTION64_FLOOR_ERROR;assert(motion64_query_floor(1.25f,254.5f,-3.75f,&height,&floor)&&!floor);assert(motion64_floor_provider_faults()==1);
 c.result=MOTION64_FLOOR_HIT;c.floor.normal.y=0;
 assert(motion64_query_floor(1.25f,254.5f,-3.75f,&height,&floor)&&!floor);assert(motion64_floor_provider_faults()==2);
 const unsigned calls=c.calls;assert(motion64_query_floor(NAN,1,2,&height,&floor)&&!floor&&c.calls==calls);
 motion64_set_floor_provider(NULL,NULL);height=99;floor=&sentinel;
 assert(!motion64_query_floor(0,0,0,&height,&floor)&&height==99&&floor==&sentinel&&!motion64_floor_provider_faults());
 assert(!motion64_query_ceiling(0,0,0,&height,&floor)&&height==99&&floor==&sentinel);
 c.floor.normal.y=-1;c.result=MOTION64_FLOOR_HIT;
 motion64_set_ceiling_provider(provider,&c);
 assert(motion64_query_ceiling(1.25f,254.5f,-3.75f,&height,&floor)&&floor==&c.floor&&height==254.125f);
 c.result=MOTION64_FLOOR_MISSING;
 assert(motion64_query_ceiling(1.25f,254.5f,-3.75f,&height,&floor)&&!floor&&height==100000.f&&!motion64_ceiling_provider_faults());
 c.result=MOTION64_FLOOR_HIT;c.floor.normal.y=1;
 assert(motion64_query_ceiling(1.25f,254.5f,-3.75f,&height,&floor)&&!floor&&motion64_ceiling_provider_faults()==1);
 motion64_set_ceiling_provider(NULL,NULL);height=77;floor=&sentinel;
 assert(!motion64_query_ceiling(0,0,0,&height,&floor)&&height==77&&floor==&sentinel);
 struct WallContext w;memset(&w,0,sizeof w);w.wall.isValid=1;w.wall.normal.x=1;
 struct SM64WallCollisionData data;memset(&data,0,sizeof data);data.x=1;data.y=2;data.z=3;data.radius=50;
 int count=99;assert(!motion64_query_walls(&data,&count)&&data.x==1&&count==99);
 motion64_set_wall_provider(wall_provider,&w);
 assert(motion64_query_walls(&data,&count)&&count==1&&data.x==2&&data.numWalls==1&&data.walls[0]==&w.wall);
 for(int mode=1;mode<=6;++mode){
  w.mode=mode;assert(motion64_query_walls(&data,&count)&&count==0&&data.x==2&&data.y==2&&data.z==3&&!data.numWalls&&!data.walls[0]);
 }
 assert(motion64_wall_provider_faults()==5);
 const unsigned calls_before=w.calls;data.radius=-1;
 assert(motion64_query_walls(&data,&count)&&count==0&&w.calls==calls_before);
 motion64_set_wall_provider(NULL,NULL);assert(!motion64_query_walls(&data,&count));
 struct SM64SurfaceObjectTransform handle;memset(&handle,0,sizeof handle);
 float position[3]={1,2,3};int16_t yaw=123;
 assert(!motion64_query_platform(&handle,position,&yaw)&&position[0]==1&&yaw==123);
 struct PlatformContext p={0};motion64_set_platform_provider(platform_provider,&p);
 assert(motion64_query_platform(&handle,position,&yaw)==1&&position[0]==11&&position[1]==22&&position[2]==-27&&yaw==16384);
 for(int mode=1;mode<=4;++mode){
  p.mode=mode;float previous[3];memcpy(previous,position,sizeof previous);yaw=123;
  assert(motion64_query_platform(&handle,position,&yaw)==-1&&!memcmp(previous,position,sizeof previous)&&yaw==123);
 }
 const unsigned platform_calls=p.calls;
 assert(motion64_query_platform(NULL,position,&yaw)==-1&&p.calls==platform_calls);
 assert(motion64_query_platform(&handle,NULL,&yaw)==-1&&p.calls==platform_calls);
 assert(motion64_query_platform(&handle,position,NULL)==-1&&p.calls==platform_calls);
 position[0]=NAN;assert(motion64_query_platform(&handle,position,&yaw)==-1&&p.calls==platform_calls);
 assert(motion64_platform_provider_faults()==8);
 motion64_set_platform_provider(NULL,NULL);assert(!motion64_platform_provider_faults());
 position[0]=1;assert(!motion64_query_platform(&handle,position,&yaw)&&position[0]==1&&yaw==123);
}
