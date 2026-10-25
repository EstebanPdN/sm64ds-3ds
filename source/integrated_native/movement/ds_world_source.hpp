#pragma once
#include "world_session.hpp"
#include "world_walls.hpp"
#include "stable_platforms.hpp"
#include "surface_description.hpp"

namespace movement_geometry {
// Scene-owned native DS backend for WorldSession. The owner must outlive the
// session and its retained contacts. Prepare/reset only after deleting every
// solver; static scene/KCL changes require a fresh prepare before activation.
// Keep this resident owner outside the 3DS simulation stack.
template<std::size_t WallCapacity=1024,std::size_t PlatformCapacity=32> class DsWorldSource {
    WorldWalls<WallCapacity> walls;
    StablePlatforms<PlatformCapacity> platforms;
    bool prepared=false;
    MaterialUse delegation_=MaterialUse::Solid;
    static int count_actor(const Motion64Contacts*,void*){return 1;}
    static int floor_cb(const int32_t* position,void* actor,DsWorldGroundHit* hit,void* context) {
        return static_cast<DsWorldSource*>(context)->prepared?sm64ds_motion_ground(position,actor,hit):-1;
    }
    static int ceiling_cb(const int32_t* position,void* actor,DsWorldGroundHit* hit,void* context) {
        if(!static_cast<DsWorldSource*>(context)->prepared)return -1;
        DsWorldGroundHit staged;
        const int status=sm64ds_motion_ceiling(position,actor,&staged);
        if(status!=1)return status;
        const auto profile=material_profile(staged.clps[0],staged.clps[1]);
        if(distant_effect_ceiling(profile,staged.height,position[1]))return 0;
        *hit=staged;return 1;
    }
    static int walls_cb(const SM64WallCollisionData* input,void* actor,WorldWallContacts* output,void* context) {
        auto& self=*static_cast<DsWorldSource*>(context);if(!self.prepared)return -1;
        const DsWorldWallInput query{input->x,input->y,input->z,input->offsetY,input->radius};
        DsWorldWallResult result;
        const int status=self.walls.query_prepared(&query,actor,1,&result);
        if(status==-2){self.delegation_=MaterialUse::NeedsDsEffect;return -1;}
        if(status!=1)return status;
        WorldWallContacts staged;staged.x=result.x;staged.z=result.z;staged.count=result.count;staged.stored=result.stored;
        for(int i=0;i<result.stored;++i)staged.surfaces[i]=result.surfaces[i];
        *output=staged;return 1;
    }
    static bool describe_cb(const DsWorldGroundHit* hit,SurfaceKey* key,SM64SurfaceCollisionData* surface,void* context) {
        auto& self=*static_cast<DsWorldSource*>(context);if(!self.prepared)return false;
        DsWorldPrism prism;if(!sm64ds_contact_prism(hit,&prism))return false;
        const auto use=material_profile(hit->clps[0],hit->clps[1]).use;
        if(use!=MaterialUse::Solid){self.delegation_=use;return false;}
        SM64SurfaceObjectTransform* handle=nullptr;
        if(prism.kind) {
            const DsWorldPlatformRef ref{hit->collider,hit->actor,prism.kcl,hit->owner_id,hit->slot,prism.kind};
            handle=self.platforms.acquire(ref);if(!handle)return false;
        }
        SurfaceDescription description;
        const auto status=describe_surface(*hit,prism,handle,description);
        if(status==SurfaceDescribe::NeedsDsEffect)self.delegation_=MaterialUse::NeedsDsEffect;
        if(status!=SurfaceDescribe::Ready)return false;
        *key=description.key;*surface=description.surface;return true;
    }
    static bool retain_cb(const SM64SurfaceObjectTransform* const* pins,std::size_t count,void* context) {
        auto& self=*static_cast<DsWorldSource*>(context);
        self.delegation_=MaterialUse::Solid;
        return self.prepared&&self.walls.begin_tick()&&self.platforms.begin_tick(pins,count);
    }
    static int platform_cb(const SM64SurfaceObjectTransform* handle,float position[3],int16_t* yaw,void* context) {
        auto& self=*static_cast<DsWorldSource*>(context);
        return self.prepared?self.platforms.displace(handle,position,yaw):-1;
    }
public:
    DsWorldSource()=default;
    DsWorldSource(const DsWorldSource&)=delete;
    DsWorldSource& operator=(const DsWorldSource&)=delete;
    bool prepare() {
        if(active_world_session || motion64_visit_contacts(count_actor,nullptr)!=0)return false;
        // DS abilities temporarily own movement. Revalidate the scene cache
        // on resumption instead of decoding every static prism again.
        if(prepared && walls.begin_tick()){delegation_=MaterialUse::Solid;return true;}
        prepared=false;delegation_=MaterialUse::Solid;platforms.reset_after_solver_deleted();
        if(!walls.build())return false;
        prepared=true;return true;
    }
    bool reset_contacts_after_session_closed() {
        if(active_world_session || motion64_visit_contacts(count_actor,nullptr)!=0)return false;
        delegation_=MaterialUse::Solid;platforms.reset_after_solver_deleted();return true;
    }
    bool reset_after_session_closed() {
        if(active_world_session || motion64_visit_contacts(count_actor,nullptr)!=0)return false;
        prepared=false;delegation_=MaterialUse::Solid;walls.clear();platforms.reset_after_solver_deleted();return true;
    }
    MaterialUse delegation()const{return delegation_;}
    bool generation_changed()const{return walls.generation_changed();}
    WorldContactSource callbacks() {
        return {this,floor_cb,ceiling_cb,walls_cb,describe_cb,retain_cb,platform_cb};
    }
};
}
