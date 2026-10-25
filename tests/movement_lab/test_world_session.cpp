#include "world_session.hpp"
#include "wall_contact.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <algorithm>
using namespace movement_geometry;
struct World {
    int roof=900;
    bool wall=true,refuse=false;
    unsigned floor_calls=0,ceiling_calls=0,wall_calls=0,descriptions=0;
    bool moving=false,transport_error=false,pins_error=false;
    float transport_step=2;
    unsigned transport_calls=0,retains=0;
    SM64SurfaceObjectTransform platform{};
};
static DsWorldGroundHit hit(int face,int height) {
    DsWorldGroundHit h{};h.height=height*4096;h.normal[face==3?0:1]=face==1?4096:-4096;
    h.collider=0x1000*face;h.owner_id=-1;h.triangle=uint16_t(face);h.slot=uint16_t(face-1);return h;
}
static int floor_query(const int32_t* p,void*,DsWorldGroundHit* out,void* context) {
    ++static_cast<World*>(context)->floor_calls;if(p[1]+78<254)return 0;*out=hit(1,254);return 1;
}
static int ceiling_query(const int32_t* p,void*,DsWorldGroundHit* out,void* context) {
    auto& world=*static_cast<World*>(context);++world.ceiling_calls;if(p[1]-78>world.roof)return 0;*out=hit(2,world.roof);return 1;
}
static int wall_query(const SM64WallCollisionData* input,void*,WorldWallContacts* out,void* context) {
    auto& world=*static_cast<World*>(context);++world.wall_calls;
    if(!world.wall)return 0;
    WorldTriangle wall{};wall.geometry.normal={-1,0,0};wall.plane_anchor={150,0,0};
    wall.geometry.vertex[0]={150,-12000,-12000};wall.geometry.vertex[1]={150,-12000,12000};wall.geometry.vertex[2]={150,12000,0};
    WallPush push{};
    const auto status=wall_contact(wall,{input->x,input->y,input->z,input->offsetY,input->radius},push);
    if(status==WallContact::Invalid)return -1;
    if(status==WallContact::Missing)return 0;
    out->x=input->x+push.x;out->z=input->z+push.z;out->count=out->stored=1;out->surfaces[0]=hit(3,0);return 1;
}
static bool describe(const DsWorldGroundHit* h,SurfaceKey* key,SM64SurfaceCollisionData* out,void* context) {
    auto& world=*static_cast<World*>(context);++world.descriptions;if(world.refuse)return false;
    *key={h->collider,h->actor,uint32_t(h->owner_id),uint32_t(0x5000+h->slot),h->triangle,h->slot};
    *out={};out->isValid=1;out->normal={h->normal[0]/4096.f,h->normal[1]/4096.f,h->normal[2]/4096.f};
    out->originOffset=h->triangle==1?-254.f:h->triangle==2?float(world.roof):150.f;
    out->lowerY=-12000;out->upperY=12000;
    if(world.moving&&h->triangle==1)out->transform=&world.platform;
    return true;
}
static bool platform_pins(const SM64SurfaceObjectTransform* const* pins,std::size_t count,void* context) {
    auto& world=*static_cast<World*>(context);++world.retains;
    if(world.pins_error)return false;
    for(std::size_t i=0;i<count;++i)if(pins[i]!=&world.platform)return false;
    return true;
}
static int transport(const SM64SurfaceObjectTransform* handle,float position[3],int16_t* yaw,void* context) {
    auto& world=*static_cast<World*>(context);++world.transport_calls;
    if(handle!=&world.platform || world.transport_error)return -1;
    position[0]+=world.transport_step;*yaw=0;return 1;
}
static WorldContactSource source(World& w) {return {&w,floor_query,ceiling_query,wall_query,describe,w.moving?platform_pins:nullptr,w.moving?transport:nullptr};}
static int alive(const Motion64Contacts*,void*) {return 1;}
int main() {
    World world;
    float maximum_x=-1000,maximum_y=0;
    {
        WorldSession<32,2> session;assert(session.open(source(world)));
        WorldSession<32,2> competing;assert(!competing.open(source(world)));
        const int first=session.create(-100,300,0,nullptr),second=session.create(-200,300,0,nullptr);
        assert(first>=0&&second>=0&&first!=second);
        assert(session.create(0,300,0,nullptr)<0 && session.create(NAN,0,0,nullptr)<0);
        SM64MarioState state{};
        for(unsigned tick=0;tick<240;++tick) {
            SM64MarioInputs input{0,1,-1,0,uint8_t(tick%60<10),0,0};
            assert(session.tick(first,input,state));
            maximum_x=std::max(maximum_x,state.position[0]);maximum_y=std::max(maximum_y,state.position[1]);
            assert(state.position[0]<=150 && state.position[1]>=254);
            SM64MarioInputs idle{0,1,0,0,0,0,0};assert(session.tick(second,idle,state));
        }
        assert(maximum_x>0 && maximum_y>300 && !session.faults());
        assert(world.floor_calls&&world.ceiling_calls&&world.wall_calls&&world.descriptions);
        SM64MarioState unchanged=state;world.refuse=true;
        SM64MarioInputs input{0,1,0,0,0,0,0};assert(!session.tick(first,input,state));
        assert(!std::memcmp(&state,&unchanged,sizeof state)&&session.faults());
        assert(!session.tick(first,input,state));
        // Destructor owns deletion before callback/cache/global teardown.
    }
    assert(!active_world_session && motion64_visit_contacts(alive,nullptr)==0);
    world.refuse=false;world.wall=false;world.roof=450;
    float roof_peak=0;
    {
        WorldSession<32,2> session;assert(session.open(source(world)));
        int mario=session.create(0,254,0,nullptr);assert(mario>=0);
        for(unsigned tick=0;tick<240;++tick) {
            SM64MarioInputs input{0,1,0,0,uint8_t(tick%60<10),0,0};SM64MarioState output{};
            assert(session.tick(mario,input,output));roof_peak=std::max(roof_peak,output.position[1]);
            assert(output.position[1]>=254 && output.position[1]<450);
        }
        assert(roof_peak>254 && !session.faults());assert(session.remove(mario));assert(!session.remove(mario));
    }
    assert(!active_world_session && motion64_visit_contacts(alive,nullptr)==0);
    world.refuse=true;
    {
        WorldSession<32,2> session;assert(session.open(source(world)));
        assert(session.create(0,254,0,nullptr)<0 && session.faults());
    }
    assert(!active_world_session && motion64_visit_contacts(alive,nullptr)==0);
    world.refuse=false;world.wall=true;world.roof=900;
    float circle_x=-1000;
    {
        WorldSession<32,2> session;assert(session.open(source(world)));
        int mario=session.create(0,254,0,nullptr);assert(mario>=0);
        for(unsigned tick=0;tick<120;++tick) {
            SM64MarioState output{};assert(session.tick_circle(mario,156,0,0,tick%60<10,false,false,output));
            circle_x=std::max(circle_x,output.position[0]);assert(output.position[0]<=150);
        }
        assert(circle_x>0 && !session.faults());
    }
    assert(!active_world_session && motion64_visit_contacts(alive,nullptr)==0);
    world.wall=false;world.moving=true;world.platform.aVelX=1000;
    {
        WorldSession<32,2> session;assert(session.open(source(world)));
        const int mario=session.create(0,254,0,nullptr);assert(mario>=0);
        SM64MarioState output{};
        for(unsigned tick=0;tick<120;++tick)assert(session.tick_circle(mario,0,0,0,false,false,false,output));
        assert(output.position[0]>100&&output.position[0]<250&&world.transport_calls>50&&world.retains>100);
        const auto previous=output;world.transport_error=true;
        assert(!session.tick_circle(mario,0,0,0,false,false,false,output)&&session.faults());
        assert(!std::memcmp(&previous,&output,sizeof output));
    }
    assert(!active_world_session && motion64_visit_contacts(alive,nullptr)==0);
    {
        auto incomplete=source(world);incomplete.platform=nullptr;
        WorldSession<32,2> session;assert(session.open(incomplete));
        assert(session.create(0,254,0,nullptr)<0&&session.faults());
    }
    assert(!active_world_session && motion64_visit_contacts(alive,nullptr)==0);
    world.moving=false;world.transport_error=false;
    {
        WorldSession<32,2> session;assert(session.open(source(world)));
        const int mario=session.create(0,254,0,nullptr);assert(mario>=0);
        sm64_set_mario_action(mario,0x0c400201);sm64_set_mario_velocity(mario,0,0,0);
        assert(session.seed_buttons(mario,true,false,false)&&!session.seed_buttons(-1,true,false,false));
        SM64MarioState output{};
        for(unsigned tick=0;tick<60;++tick) {
            assert(session.tick_circle(mario,0,0,0,true,false,false,output));
            assert(output.position[1]==254&&output.velocity[1]==0);
        }
        assert(session.tick_circle(mario,0,0,0,false,false,false,output));
        assert(session.tick_circle(mario,0,0,0,true,false,false,output));
        assert(output.position[1]>254&&output.velocity[1]>0);
    }
    assert(!active_world_session && motion64_visit_contacts(alive,nullptr)==0);
    world.moving=true;
    {
        WorldSession<32,2> session;assert(session.open(source(world)));
        const int mario=session.create(0,254,0,nullptr);assert(mario>=0);
        SM64MarioState output{};
        assert(session.tick_circle(mario,0,0,0,false,false,false,output));
        const auto saved=output;world.pins_error=true;
        assert(!session.tick_circle(mario,0,0,0,false,false,false,output));
        assert(session.faults()&&session.begin_failure_stage()==2&&!std::memcmp(&saved,&output,sizeof output));
        world.pins_error=false;
        assert(!session.tick_circle(mario,0,0,0,false,false,false,output)); // failure stays latched
    }
    {
        WorldSession<32,2> session;assert(session.open(source(world)));
        const int mario=session.create(0,254,0,nullptr);assert(mario>=0);
        SM64MarioState output{};
        assert(session.tick_circle(mario,0,0,0,false,false,false,output));
        const auto saved=output;world.transport_step=600000; // finite, outside DS signed Fix12
        assert(!session.tick_circle(mario,0,0,0,false,false,false,output));
        assert(session.faults()&&!std::memcmp(&saved,&output,sizeof output));
    }
    assert(!active_world_session && motion64_visit_contacts(alive,nullptr)==0);
    std::printf("world session: real-core ground/wall/ceiling/platform cases and62button-history steps, wall X %.3f, jump Y %.3f, low-roof Y %.3f, CirclePad X %.3f; held history, latched begin failures, Fix12 overflow staging and teardown passed\n",maximum_x,maximum_y,roof_peak,circle_x);
}
