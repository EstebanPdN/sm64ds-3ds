// Offline ARM replay only: explicit collider query for comparison with the
// production bridge's original full-registry query. Never linked into the game.
#include "ds_world_query.h"
#include "dBgCh_Gnd.h"
#include "dBgW_Kc.h"
#include "dBgW_KcMbg.h"
#include "dBgCh_Lin.h"
#include "SurfaceInfo.h"
#include "world_walls.hpp"
static movement_geometry::WorldWalls<1024> saved_wall_cache;
extern "C" int fixture_wall_cache_build() {return saved_wall_cache.build()?1:0;}
extern "C" unsigned fixture_wall_cache_size() {return saved_wall_cache.size();}
extern "C" unsigned fixture_wall_cache_reads() {return saved_wall_cache.prism_reads();}
extern "C" int fixture_wall_cache_query(const DsWorldWallInput* input,void* actor,
                                        uint8_t flags,DsWorldWallResult* result) {
    return saved_wall_cache.query(input,actor,flags,result);
}
extern "C" dBgW* data_020a0c80[];
extern "C" int fixture_original_platform_displace(void* pointer,const int32_t* position,int32_t* result) {
    auto* collider=static_cast<dBgW*>(pointer);
    Vector3 input{int(position[0]),int(position[1]),int(position[2])},output;
    if(!collider->TransformPos(input,output))return 0;
    const int32_t staged[]={int(output.x),int(output.y),int(output.z),int(collider->GetAngularVelY())};
    for(unsigned i=0;i<4;++i)result[i]=staged[i];return 1;
}
extern "C" int fixture_prism_contact(unsigned slot,unsigned triangle,DsWorldGroundHit* hit) {
    if(slot>=24 || !triangle || triangle>=32768 || !hit || !data_020a0c80[slot])return 0;
    auto* collider=data_020a0c80[slot];SurfaceInfo surface;
    collider->GetSurfaceInfo(int16_t(triangle),surface);
    DsWorldGroundHit result{};
    result.normal[0]=int(surface.normal.x);result.normal[1]=int(surface.normal.y);result.normal[2]=int(surface.normal.z);
    result.clps[0]=surface.clps.w0;result.clps[1]=surface.clps.w1;
    result.collider=uint32_t(uintptr_t(collider));result.actor=uint32_t(uintptr_t(collider->owner));
    result.owner_id=collider->ownerUniqueID;result.triangle=triangle;result.slot=slot;
    *hit=result;return 1;
}
extern "C" void func_02037464(void*);
extern "C" int fixture_collider_ground(void* collider,const int32_t* position,
                                       int32_t depth,DsWorldGroundHit* hit) {
    dBgCh_Gnd query;
    Vector3 point{int(position[0]),int(position[1]),int(position[2])};
    query.SetObjAndPos(point,nullptr);
    query.StartDetectingWater();query.StopDetectingOrdinary();
    query.mProbeHeight=depth;
    func_02037464(&query);
    if (!static_cast<dBgW_Kc*>(collider)->DetectClsn(query) || !query.hasClsn) return 0;
    DsWorldGroundHit result{};
    result.height=query.clsnY;
    result.normal[0]=query.surface.normal.x;
    result.normal[1]=query.surface.normal.y;
    result.normal[2]=query.surface.normal.z;
    result.clps[0]=query.surface.clps.w0;result.clps[1]=query.surface.clps.w1;
    result.triangle=query.triangleID;
    *hit=result;
    return 1;
}
extern "C" void fixture_transform_collider(void* collider,const Matrix4x3* matrix,
                                           short yaw) {
    static_cast<dBgW_KcMbg*>(collider)->Transform(*matrix,yaw);
}
extern "C" int fixture_collider_line(void* collider,const int32_t* endpoints,
                                     DsWorldLineHit* hit) {
    dBgCh_Lin query;
    Vector3 a{int(endpoints[0]),int(endpoints[1]),int(endpoints[2])};
    Vector3 b{int(endpoints[3]),int(endpoints[4]),int(endpoints[5])};
    query.SetObjAndLine(a,b,nullptr);
    if (!static_cast<dBgW_Kc*>(collider)->DetectClsn(query) || !query.hasClsn) return 0;
    DsWorldLineHit result{};
    result.position[0]=query.lineEnd.x;result.position[1]=query.lineEnd.y;result.position[2]=query.lineEnd.z;
    result.normal[0]=query.surface.normal.x;result.normal[1]=query.surface.normal.y;result.normal[2]=query.surface.normal.z;
    result.clps[0]=query.surface.clps.w0;result.clps[1]=query.surface.clps.w1;
    result.triangle=query.triangleID;*hit=result;return 1;
}
