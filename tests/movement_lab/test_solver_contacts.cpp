#include "libsm64.h"
#include "solver_contacts.h"
#include "solver_contact_pins.hpp"
#include "stable_surfaces.hpp"
#include "collision_provider.h"
#include "castle_surfaces.inc"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cmath>
extern "C" void* fixture_bound_state();
extern "C" void fixture_set_solver_contacts(int,SM64SurfaceCollisionData**,SM64SurfaceObjectTransform*,int);
extern "C" void fixture_core_platform_step(int,SM64SurfaceObjectTransform*,const float*,float*,short*);
struct TransportContext {unsigned calls=0;bool fail=false;};
static int transport(const SM64SurfaceObjectTransform*,float position[3],int16_t* yaw,void* opaque) {
    auto& context=*static_cast<TransportContext*>(opaque);++context.calls;
    position[0]+=20;position[1]+=20;position[2]+=20;*yaw=16384;
    return context.fail?-1:1;
}
struct FloorContext {movement_geometry::StableSurfaces<16>* cache;unsigned generation;};
struct CeilingContext {SM64SurfaceCollisionData surface{};float height;bool missing=false;};
static int mock_ceiling(float,float,float,float* height,SM64SurfaceCollisionData** out,void* opaque) {
    auto& context=*static_cast<CeilingContext*>(opaque);
    if (context.missing) return MOTION64_FLOOR_MISSING;
    *height=context.height;*out=&context.surface;return MOTION64_FLOOR_HIT;
}
static int cached_floor(float,float,float,float* height,SM64SurfaceCollisionData** out,void* opaque) {
    auto& context=*static_cast<FloorContext*>(opaque);
    auto* surface=context.cache->acquire({0x9000000,0x9100000,context.generation,0x9200000,1,0});
    if (!surface) return MOTION64_FLOOR_ERROR;
    surface->normal={0,1,0};surface->originOffset=-254;surface->isValid=1;
    *height=254;*out=surface;return MOTION64_FLOOR_HIT;
}
int main() {
    sm64_global_init(nullptr,nullptr);
    sm64_static_surfaces_load(castle_surfaces,sizeof castle_surfaces/sizeof *castle_surfaces);
    float probe[3]{},original_height=0;SM64SurfaceCollisionData* original_ceiling=nullptr;
    for (const auto& surface:castle_surfaces) {
        for(unsigned i=0;i<3;++i) probe[i]=(surface.vertices[0][i]+surface.vertices[1][i]+surface.vertices[2][i])/3.f;
        probe[1]-=10;
        original_height=sm64_surface_find_ceil(probe[0],probe[1],probe[2],&original_ceiling);
        if(original_ceiling) break;
    }
    assert(original_ceiling);
    CeilingContext ceiling_context;ceiling_context.height=probe[1]+100;
    ceiling_context.surface.isValid=1;ceiling_context.surface.normal={0,-1,0};ceiling_context.surface.originOffset=ceiling_context.height;
    motion64_set_ceiling_provider(mock_ceiling,&ceiling_context);
    SM64SurfaceCollisionData* actual_ceiling=nullptr;
    assert(sm64_surface_find_ceil(probe[0],probe[1],probe[2],&actual_ceiling)==ceiling_context.height && actual_ceiling==&ceiling_context.surface);
    ceiling_context.missing=true;
    assert(sm64_surface_find_ceil(probe[0],probe[1],probe[2],&actual_ceiling)==100000 && !actual_ceiling && !motion64_ceiling_provider_faults());
    motion64_set_ceiling_provider(nullptr,nullptr);
    assert(sm64_surface_find_ceil(probe[0],probe[1],probe[2],&actual_ceiling)==original_height && actual_ceiling==original_ceiling);
    int first=sm64_mario_create(-1200,1000,6800),second=sm64_mario_create(-1000,1000,6800);
    assert(first>=0 && second>=0 && first!=second);
    void* seed_bound=fixture_bound_state();
    assert(motion64_seed_buttons(first,1,1,1)&&fixture_bound_state()==seed_bound);
    assert(!motion64_seed_buttons(-1,1,1,1)&&!motion64_seed_buttons(100000,1,1,1));
    assert(motion64_seed_buttons(first,0,0,0)&&fixture_bound_state()==seed_bound);
    SM64SurfaceObjectTransform legacy_platform{};legacy_platform.aVelX=10;legacy_platform.aVelZ=30;
    const float transport_input[]={1,2,3};float transport_output[3];short transport_yaw=0;
    fixture_core_platform_step(first,&legacy_platform,transport_input,transport_output,&transport_yaw);
    assert(transport_output[0]==11&&transport_output[1]==2&&transport_output[2]==33&&!transport_yaw);
    TransportContext transport_context;motion64_set_platform_provider(transport,&transport_context);
    fixture_core_platform_step(first,&legacy_platform,transport_input,transport_output,&transport_yaw);
    assert(transport_output[0]==21&&transport_output[1]==22&&transport_output[2]==23&&transport_yaw==16384&&transport_context.calls==1);
    transport_context.fail=true;
    fixture_core_platform_step(first,&legacy_platform,transport_input,transport_output,&transport_yaw);
    assert(!std::memcmp(transport_output,transport_input,sizeof transport_input)&&!transport_yaw&&motion64_platform_provider_faults()==1);
    motion64_set_platform_provider(nullptr,nullptr);
    fixture_core_platform_step(first,&legacy_platform,transport_input,transport_output,&transport_yaw);
    assert(transport_output[0]==11&&transport_output[1]==2&&transport_output[2]==33&&!transport_yaw);
    void* bound=fixture_bound_state();
    Motion64Contacts contacts{},sentinel{};
    assert(motion64_get_contacts(first,&contacts) && contacts.floor && contacts.mario_id==first);
    assert(fixture_bound_state()==bound);
    std::memset(&contacts,0xcd,sizeof contacts);sentinel=contacts;
    assert(!motion64_get_contacts(-1,&contacts) && !std::memcmp(&contacts,&sentinel,sizeof contacts));
    assert(!motion64_get_contacts(100000,&contacts) && !std::memcmp(&contacts,&sentinel,sizeof contacts));
    assert(!motion64_get_contacts(first,nullptr) && motion64_visit_contacts(nullptr,nullptr)==-1);
    movement_geometry::StableSurfaces<16> cache;
    movement_geometry::SolverContactPins<16> pins;
    assert(pins.collect() && pins.surface_size()>0);
    assert(!cache.begin_tick(pins.surface_data(),pins.surface_size())); // old mesh isn't cache-owned
    assert(cache.begin_tick(nullptr,0));
    SM64SurfaceCollisionData* surfaces[8];
    for (unsigned i=0;i<8;++i) {
        surfaces[i]=cache.acquire({0x9000000,0x9100000,1,0x9200000,uint16_t(i+1),0});assert(surfaces[i]);
        surfaces[i]->originOffset=float(100+i);surfaces[i]->isValid=1;
    }
    cache.end_tick();
    SM64SurfaceObjectTransform platform{};
    fixture_set_solver_contacts(first,surfaces,&platform,0);
    fixture_set_solver_contacts(second,surfaces+4,&platform,0);
    assert(pins.collect() && pins.surface_size()==8 && pins.platform_size()==1);
    assert(pins.platform_data()[0]==&platform && fixture_bound_state()==bound);
    SM64SurfaceObjectTransform nested[4]{};
    for(unsigned i=0;i<4;++i)surfaces[i]->transform=&nested[i];
    fixture_set_solver_contacts(first,surfaces,nullptr,0);
    fixture_set_solver_contacts(second,surfaces+4,nullptr,0);
    assert(pins.collect()&&pins.surface_size()==8&&pins.platform_size()==4);
    for(unsigned i=0;i<4;++i) {
        bool retained=false;for(std::size_t j=0;j<pins.platform_size();++j)retained|=pins.platform_data()[j]==&nested[i];
        assert(retained);
    }
    fixture_set_solver_contacts(first,surfaces,&platform,0);
    assert(pins.collect()&&pins.platform_size()==5);
    for(unsigned i=0;i<4;++i)surfaces[i]->transform=&platform;
    assert(pins.collect()&&pins.platform_size()==1); // nested/object refs dedup
    for(unsigned i=0;i<4;++i)surfaces[i]->transform=nullptr;
    fixture_set_solver_contacts(second,surfaces+4,&platform,0);
    assert(pins.collect()&&pins.platform_size()==1);
    assert(cache.begin_tick(pins.surface_data(),pins.surface_size()));
    for (unsigned i=0;i<8;++i) assert(cache.acquire({0x9000000,0x9100000,2,0x9200000,uint16_t(i+1),0}));
    assert(!cache.acquire({0x9000000,0x9100000,2,0x9200000,9,0}));cache.end_tick();
    for (unsigned i=0;i<8;++i) assert(surfaces[i]->originOffset==float(100+i));
    // A permanent engine-owned water pseudo-floor is expected ownership, not
    // an arbitrary external pointer that should bypass cache validation.
    fixture_set_solver_contacts(first,surfaces,&platform,1);
    assert(motion64_get_contacts(first,&contacts) && motion64_permanent_surface(contacts.floor));
    assert(!motion64_permanent_surface(surfaces[0]) && !motion64_permanent_surface(nullptr));
    assert(pins.collect() && pins.surface_size()==7 && pins.platform_size()==1);
    assert(cache.begin_tick(pins.surface_data(),pins.surface_size()));cache.end_tick();
    movement_geometry::SolverContactPins<2> too_small;
    assert(!too_small.collect() && !too_small.surface_size() && !too_small.platform_size());
    sm64_mario_delete(first);
    assert(!motion64_get_contacts(first,&contacts));
    assert(pins.collect() && pins.surface_size()==4 && pins.platform_size()==1);
    assert(cache.begin_tick(pins.surface_data(),pins.surface_size()));cache.end_tick();
    sm64_mario_delete(second);
    assert(pins.collect() && pins.surface_size()==0 && pins.platform_size()==0);
    cache.reset_after_solver_deleted();
    // Actual N64 stationary/jump ticks on an explicit flat-plane fixture.
    // The owner generation changes each tick, exercising retained-pointer
    // protection across real solver updates rather than just container calls.
    sm64_static_surfaces_load(nullptr,0);
    FloorContext context{&cache,10};motion64_set_floor_provider(cached_floor,&context);
    assert(cache.begin_tick(nullptr,0));int mario=sm64_mario_create(-1200,300,6800);cache.end_tick();assert(mario>=0);
    float maximum_y=0;
    for (unsigned tick=0;tick<360;++tick) {
        assert(pins.collect() && cache.begin_tick(pins.surface_data(),pins.surface_size()));
        ++context.generation;
        SM64MarioInputs input{0,1,0,0,uint8_t(tick%60<10),0,0};SM64MarioState state{};
        sm64_mario_tick(mario,&input,&state,nullptr);
        assert(std::isfinite(state.position[1]) && state.position[1]>=254);
        if (state.position[1]>maximum_y) maximum_y=state.position[1];
        assert(motion64_get_contacts(mario,&contacts) && contacts.floor && contacts.floor->isValid && contacts.floor->normal.y==1);
        // Fill remaining unprotected slots after the solver's last query.
        // This must not overwrite its newly retained contact.
        const auto* floor=contacts.floor;
        for (unsigned i=1;i<=16;++i) {
            auto* churn=cache.acquire({0x9000000,0x9100000,context.generation,0x9200000,uint16_t(i+1),0});
            if (!churn) break;
            churn->normal={1,0,0};churn->originOffset=float(i);
        }
        assert(floor->normal.y==1 && floor->originOffset==-254);
        cache.end_tick();
    }
    assert(maximum_y>300 && !motion64_floor_provider_faults());
    sm64_mario_delete(mario);motion64_set_floor_provider(nullptr,nullptr);
    assert(pins.collect() && !pins.surface_size());cache.reset_after_solver_deleted();
    sm64_global_terminate();
    std::printf("actual N64 instances: ceiling routing/no-fallback, contact ownership and deletion passed; 360 jumping ticks with cache churn passed (max Y %.3f)\n",maximum_y);
}
