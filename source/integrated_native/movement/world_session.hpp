#pragma once
// Exclusive headless-core session. Main-game activation still requires the
// real DS wall solver, CLPS/geometry resolver and platform owner; no fake
// movement-mode selector or approximate mesh fallback is supplied here. Keep
// the256-slot instance in resident/static storage or an owner allocation made
// outside simulation; it is intentionally larger than the3DS main stack.
#include "collision_provider.h"
#include "ds_world_query.h"
#include "solver_contact_pins.hpp"
#include "stable_surfaces.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <climits>
#include <initializer_list>
#include <algorithm>
extern "C" void motion64_tick_circle(int,int,int,short,int,int,int,SM64MarioState*);
namespace movement_geometry {
struct WorldWallContacts {
    float x=0,z=0;
    int count=0,stored=0;
    std::array<DsWorldGroundHit,4> surfaces{};
};
struct WorldContactSource {
    void* context=nullptr;
    int (*floor)(const int32_t*,void* actor,DsWorldGroundHit*,void*)=nullptr;
    int (*ceiling)(const int32_t*,void* actor,DsWorldGroundHit*,void*)=nullptr;
    int (*walls)(const SM64WallCollisionData*,void* actor,WorldWallContacts*,void*)=nullptr;
    // Returns validated geometry/properties and its original KCL identity.
    // Must preserve DS normals, explicitly map CLPS, and retain transforms.
    bool (*describe)(const DsWorldGroundHit*,SurfaceKey*,SM64SurfaceCollisionData*,void*)=nullptr;
    bool (*retain_platforms)(const SM64SurfaceObjectTransform* const*,std::size_t,void*)=nullptr;
    int (*platform)(const SM64SurfaceObjectTransform*,float position[3],int16_t* yaw_delta,void*)=nullptr;
};
inline void* active_world_session=nullptr;
template<std::size_t Capacity=256,std::size_t Actors=4> class WorldSession {
    struct Actor {int32_t id=-1;void* source=nullptr;};
    std::array<Actor,Actors> actors{};
    StableSurfaces<Capacity> cache;
    SolverContactPins<Capacity> pins;
    WorldContactSource source{};
    void* current_actor=nullptr;
    uint32_t errors=0;
    unsigned begin_failure=0;
    bool opened=false;
    static int count_actor(const Motion64Contacts*,void*) {return 1;}
    int fail() {++errors;return -1;}
    bool begin() {
        begin_failure=0;
        if(errors || !pins.collect()){begin_failure=1;return false;}
        if(source.retain_platforms) {
            if(!source.retain_platforms(pins.platform_data(),pins.platform_size(),source.context)){begin_failure=2;return false;}
        } else if(pins.platform_size()){begin_failure=3;return false;}
        if(!cache.begin_tick(pins.surface_data(),pins.surface_size())){begin_failure=4;return false;}
        return true;
    }
    SM64SurfaceCollisionData* resolve(const DsWorldGroundHit& hit) {
        SurfaceKey key{};SM64SurfaceCollisionData value{};
        if(!source.describe(&hit,&key,&value,source.context) ||
           (value.transform && (!source.retain_platforms || !source.platform)) ||
           key.collider!=hit.collider || key.actor!=hit.actor || key.owner_id!=uint32_t(hit.owner_id) ||
           key.triangle!=hit.triangle || key.slot!=hit.slot || !key.kcl || !value.isValid ||
           !std::isfinite(value.originOffset) || value.normal.x!=hit.normal[0]/4096.f ||
           value.normal.y!=hit.normal[1]/4096.f || value.normal.z!=hit.normal[2]/4096.f) return nullptr;
        auto* slot=cache.acquire(key);if(!slot)return nullptr;
        *slot=value;return slot;
    }
    int plane(bool ceiling,float x,float y,float z,float* height,SM64SurfaceCollisionData** out) {
        if(errors)return MOTION64_FLOOR_ERROR;
        const float input[3]={x,y,z};int32_t position[3];
        for(unsigned i=0;i<3;++i) {
            if(!std::isfinite(input[i]) || double(input[i])<INT_MIN || double(input[i])>INT_MAX) return fail();
            position[i]=int32_t(input[i]); // same truncation as N64's list query
        }
        DsWorldGroundHit hit{};
        const int result=(ceiling?source.ceiling:source.floor)(position,current_actor,&hit,source.context);
        if(!result)return MOTION64_FLOOR_MISSING;
        if(result!=1)return fail();
        auto* surface=resolve(hit);if(!surface)return fail();
        if(ceiling?surface->normal.y>=-.01f:surface->normal.y<=.01f)return fail();
        *height=hit.height/4096.f;*out=surface;return MOTION64_FLOOR_HIT;
    }
    static int floor(float x,float y,float z,float* height,SM64SurfaceCollisionData** out,void* self) {
        return static_cast<WorldSession*>(self)->plane(false,x,y,z,height,out);
    }
    static int ceiling(float x,float y,float z,float* height,SM64SurfaceCollisionData** out,void* self) {
        return static_cast<WorldSession*>(self)->plane(true,x,y,z,height,out);
    }
    static int platform(const SM64SurfaceObjectTransform* handle,float position[3],int16_t* yaw,void* self) {
        auto& session=*static_cast<WorldSession*>(self);
        if(session.errors || !session.source.platform)return -1;
        const int result=session.source.platform(handle,position,yaw,session.source.context);
        return result==1?1:session.fail();
    }
    static int walls(SM64WallCollisionData* data,void* self) {
        auto& session=*static_cast<WorldSession*>(self);
        if(session.errors)return -1;
        WorldWallContacts result;result.x=data->x;result.z=data->z;
        const int status=session.source.walls(data,session.current_actor,&result,session.source.context);
        if(!status)return 0;
        if(status!=1 || result.count<=0 || result.stored<=0 || result.stored>4 || result.count<result.stored) return session.fail();
        for(int i=0;i<result.stored;++i) {
            auto* surface=session.resolve(result.surfaces[i]);if(!surface)return session.fail();
            data->walls[i]=surface;
        }
        data->x=result.x;data->z=result.z;data->numWalls=int16_t(result.stored);return result.count;
    }
    template<class Step> bool run_tick(int32_t id,Step step,SM64MarioState& output) {
        if(!opened || errors)return false;
        Actor* actor=nullptr;for(auto& a:actors)if(a.id==id && id>=0){actor=&a;break;}
        if(!actor)return false;
        if(!begin()){++errors;return false;}
        current_actor=actor->source;SM64MarioState staged{};step(staged);cache.end_tick();
        bool valid=!errors && !motion64_floor_provider_faults() && !motion64_ceiling_provider_faults() && !motion64_wall_provider_faults() && !motion64_platform_provider_faults();
        // The DS owner commits these as signed Fix12 values. Finite values
        // alone do not make lround/casts representable at that boundary.
        // At these endpoints float spacing is already 128 Fix12 units, so
        // comparisons also bound the subsequent rounded conversion exactly.
        const auto fixed12=[](float value){return value>=-524288.f&&value<524288.f;};
        for(unsigned i=0;i<3;++i)valid&=fixed12(staged.position[i])&&fixed12(staged.velocity[i]);
        valid&=std::isfinite(staged.faceAngle)&&fixed12(staged.forwardVelocity);
        if(!valid){++errors;return false;}
        output=staged;return true;
    }
public:
    WorldSession()=default;
    WorldSession(const WorldSession&)=delete;
    WorldSession& operator=(const WorldSession&)=delete;
    ~WorldSession(){close();}
    bool open(const WorldContactSource& input) {
        if(opened || active_world_session || !input.floor || !input.ceiling || !input.walls || !input.describe ||
           motion64_visit_contacts(count_actor,nullptr)!=0)return false;
        sm64_global_init(nullptr,nullptr);sm64_static_surfaces_load(nullptr,0);
        source=input;errors=0;current_actor=nullptr;
        active_world_session=this;opened=true;
        motion64_set_floor_provider(floor,this);motion64_set_ceiling_provider(ceiling,this);motion64_set_wall_provider(walls,this);
        motion64_set_platform_provider(input.platform?platform:nullptr,input.platform?this:nullptr);
        return true;
    }
    int32_t create(float x,float y,float z,void* actor) {
        if(!opened || errors)return -1;
        // The imported engine's spawn record is still Vec3s. Reject values
        // outside that ABI rather than silently wrapping an invalid spawn.
        for(float value:{x,y,z})if(!std::isfinite(value)||value<-32768.f||value>32767.f)return -1;
        Actor* slot=nullptr;for(auto& a:actors)if(a.id<0){slot=&a;break;}
        if(!slot)return -1;
        if(!begin()){++errors;return -1;}
        current_actor=actor;const int32_t id=sm64_mario_create(x,y,z);cache.end_tick();
        if(id<0)return -1;
        if(errors || motion64_floor_provider_faults() || motion64_ceiling_provider_faults() || motion64_wall_provider_faults() || motion64_platform_provider_faults()) {
            sm64_mario_delete(id);++errors;return -1;
        }
        *slot={id,actor};return id;
    }
    bool tick(int32_t id,const SM64MarioInputs& input,SM64MarioState& output) {
        for(float value:{input.camLookX,input.camLookZ,input.stickX,input.stickY})
            if(!std::isfinite(value)||std::abs(value)>1.f)return false;
        return run_tick(id,[&](SM64MarioState& staged){sm64_mario_tick(id,&input,&staged,nullptr);},output);
    }
    bool tick_circle(int32_t id,int x,int y,short camera,bool jump,bool attack,bool crouch,SM64MarioState& output) {
        // Preserve the actual N64 raw-stick adjustment imported by the lab.
        // Clamp calibrated HID values before its2:1 CirclePad conversion.
        x=std::clamp(x,-156,156);y=std::clamp(y,-156,156);
        return run_tick(id,[&](SM64MarioState& staged){motion64_tick_circle(id,x,y,camera,jump,attack,crouch,&staged);},output);
    }
    bool remove(int32_t id) {
        for(auto& a:actors)if(a.id==id && id>=0){sm64_mario_delete(id);a=Actor{};return true;}
        return false;
    }
    bool seed_buttons(int32_t id,bool jump,bool attack,bool crouch) {
        if(!opened || errors)return false;
        for(const auto& actor:actors)if(actor.id==id && id>=0)return motion64_seed_buttons(id,jump,attack,crouch)==1;
        return false;
    }
    void close() {
        if(!opened)return;
        // Delete every owned solver while its contacts are still alive.
        for(auto& a:actors)if(a.id>=0){sm64_mario_delete(a.id);a=Actor{};}
        motion64_set_floor_provider(nullptr,nullptr);motion64_set_ceiling_provider(nullptr,nullptr);motion64_set_wall_provider(nullptr,nullptr);
        motion64_set_platform_provider(nullptr,nullptr);
        sm64_global_terminate();cache.reset_after_solver_deleted();
        opened=false;current_actor=nullptr;active_world_session=nullptr;
    }
    uint32_t faults() const {return errors;}
    unsigned begin_failure_stage()const{return begin_failure;}
};
}
