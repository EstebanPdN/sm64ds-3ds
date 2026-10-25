#include "collision_provider.h"
#include "libsm64.h"
#include <math.h>
#include <stddef.h>
static Motion64FloorProvider floor_provider;
static void* floor_context;
static uint32_t provider_faults;
static Motion64FloorProvider ceiling_provider;
static void* ceiling_context;
static uint32_t ceiling_faults;
static Motion64WallProvider wall_provider;
static void* wall_context;
static uint32_t wall_faults;
static Motion64PlatformProvider platform_provider;
static void* platform_context;
static uint32_t platform_faults;
void motion64_set_floor_provider(Motion64FloorProvider provider,void* context){
    floor_provider=provider;floor_context=provider?context:NULL;provider_faults=0;
}
uint32_t motion64_floor_provider_faults(void){return provider_faults;}
int motion64_query_floor(float x,float y,float z,float* height,struct SM64SurfaceCollisionData** surface){
    if(!floor_provider)return 0;
    if(!height||!surface){++provider_faults;return 1;}
    *height=-11000.f;*surface=NULL;
    if(!isfinite(x)||!isfinite(y)||!isfinite(z)){++provider_faults;return 1;}
    float found_height=-11000.f;struct SM64SurfaceCollisionData* found=NULL;
    const int result=floor_provider(x,y,z,&found_height,&found,floor_context);
    if(result==MOTION64_FLOOR_MISSING)return 1;
    if(result!=MOTION64_FLOOR_HIT||!found||!found->isValid||!isfinite(found_height)||
       !isfinite(found->normal.x)||!isfinite(found->normal.y)||!isfinite(found->normal.z)||
       found->normal.y<=0.f||!isfinite(found->originOffset)){
        ++provider_faults;return 1;
    }
    *height=found_height;*surface=found;return 1;
}
void motion64_set_ceiling_provider(Motion64FloorProvider provider,void* context){
    ceiling_provider=provider;ceiling_context=provider?context:NULL;ceiling_faults=0;
}
uint32_t motion64_ceiling_provider_faults(void){return ceiling_faults;}
int motion64_query_ceiling(float x,float y,float z,float* height,struct SM64SurfaceCollisionData** surface){
    if(!ceiling_provider)return 0;
    if(!height||!surface){++ceiling_faults;return 1;}
    *height=100000.f;*surface=NULL;
    if(!isfinite(x)||!isfinite(y)||!isfinite(z)){++ceiling_faults;return 1;}
    float found_height=100000.f;struct SM64SurfaceCollisionData* found=NULL;
    const int result=ceiling_provider(x,y,z,&found_height,&found,ceiling_context);
    if(result==MOTION64_FLOOR_MISSING)return 1;
    if(result!=MOTION64_FLOOR_HIT||!found||!found->isValid||!isfinite(found_height)||found_height>=100000.f||
       !isfinite(found->normal.x)||!isfinite(found->normal.y)||!isfinite(found->normal.z)||
       found->normal.y>=-.01f||!isfinite(found->originOffset)){
        ++ceiling_faults;return 1;
    }
    *height=found_height;*surface=found;return 1;
}
void motion64_set_wall_provider(Motion64WallProvider provider,void* context){
    wall_provider=provider;wall_context=provider?context:NULL;wall_faults=0;
}
uint32_t motion64_wall_provider_faults(void){return wall_faults;}
int motion64_query_walls(struct SM64WallCollisionData* data,int* count){
    if(!wall_provider)return 0;
    if(!data||!count){++wall_faults;return 1;}
    *count=0;data->numWalls=0;
    for(unsigned i=0;i<4;++i)data->walls[i]=NULL;
    if(!isfinite(data->x)||!isfinite(data->y)||!isfinite(data->z)||!isfinite(data->offsetY)||!isfinite(data->radius)||data->radius<0){++wall_faults;return 1;}
    struct SM64WallCollisionData staged=*data;
    const int result=wall_provider(&staged,wall_context);
    int valid=result>=0 && staged.numWalls>=0 && staged.numWalls<=4 && result>=staged.numWalls &&
        ((result==0)==(staged.numWalls==0)) && isfinite(staged.x) && isfinite(staged.z) &&
        staged.y==data->y && staged.radius==data->radius && staged.offsetY==data->offsetY;
    valid=valid&&(result!=0||(staged.x==data->x&&staged.z==data->z));
    for(int i=0;valid&&i<staged.numWalls;++i){
        const struct SM64SurfaceCollisionData* s=staged.walls[i];
        valid=s&&s->isValid&&isfinite(s->normal.x)&&isfinite(s->normal.y)&&isfinite(s->normal.z)&&
            fabsf(s->normal.y)<=.01f&&isfinite(s->originOffset);
    }
    if(!valid){++wall_faults;return 1;}
    *data=staged;*count=result;return 1;
}
void motion64_set_platform_provider(Motion64PlatformProvider provider,void* context){
    platform_provider=provider;platform_context=provider?context:NULL;platform_faults=0;
}
uint32_t motion64_platform_provider_faults(void){return platform_faults;}
int motion64_query_platform(const struct SM64SurfaceObjectTransform* platform,float position[3],int16_t* yaw_delta){
    if(!platform_provider)return 0;
    if(!platform||!position||!yaw_delta){++platform_faults;return -1;}
    for(unsigned i=0;i<3;++i)if(!isfinite(position[i])){++platform_faults;return -1;}
    float staged[3]={position[0],position[1],position[2]};int16_t yaw=0;
    const int result=platform_provider(platform,staged,&yaw,platform_context);
    int valid=result==1;
    for(unsigned i=0;i<3;++i)valid=valid&&isfinite(staged[i]);
    if(!valid){++platform_faults;return -1;}
    for(unsigned i=0;i<3;++i)position[i]=staged[i];
    *yaw_delta=yaw;return 1;
}
