// Test-only host-to-original-ARM bridge. Fixed-width DS records cross this
// boundary; host pointers and the N64 core never enter the captured address
// space. This fixture is not compiled into the game.
#include "ds_world_source.hpp"
#include <cstring>
using namespace movement_geometry;
using Bridge=int(*)(unsigned,const void*,unsigned,void*,unsigned);
static Bridge bridge=nullptr;
static DsWorldSource<1024,32> source;
static WorldSession<256,4> session;
enum {Count=1,PrismAt,ContactPrism,PassThrough,Ground,Ceiling,Displace};
extern "C" void fixture_saved_bridge(Bridge callback){bridge=callback;}
extern "C" int sm64ds_world_prism_count(unsigned slot) {
    return bridge?bridge(Count,&slot,sizeof slot,nullptr,0):-1;
}
extern "C" int sm64ds_world_prism_at(unsigned slot,unsigned triangle,DsWorldGroundHit* hit,DsWorldPrism* prism) {
    const unsigned input[]={slot,triangle};struct {DsWorldGroundHit hit;DsWorldPrism prism;} output;
    if(!bridge || bridge(PrismAt,input,sizeof input,&output,sizeof output)!=1)return 0;
    *hit=output.hit;*prism=output.prism;return 1;
}
extern "C" int sm64ds_contact_prism(const DsWorldGroundHit* hit,DsWorldPrism* prism) {
    return bridge&&bridge(ContactPrism,hit,sizeof *hit,prism,sizeof *prism)==1?1:0;
}
extern "C" int sm64ds_contact_pass_through(const DsWorldGroundHit* hit,void* actor,uint8_t flags,uint8_t wall) {
    struct {DsWorldGroundHit hit;uint32_t actor;uint8_t flags,wall,padding[2];} input{*hit,uint32_t(uintptr_t(actor)),flags,wall,{0,0}};
    return bridge?bridge(PassThrough,&input,sizeof input,nullptr,0):-1;
}
static int plane(unsigned tag,const int32_t* position,void* actor,DsWorldGroundHit* hit) {
    struct {int32_t position[3];uint32_t actor;} input{{position[0],position[1],position[2]},uint32_t(uintptr_t(actor))};
    return bridge?bridge(tag,&input,sizeof input,hit,sizeof *hit):-1;
}
extern "C" int sm64ds_motion_ground(const int32_t* position,void* actor,DsWorldGroundHit* hit) {return plane(Ground,position,actor,hit);}
extern "C" int sm64ds_motion_ceiling(const int32_t* position,void* actor,DsWorldGroundHit* hit) {return plane(Ceiling,position,actor,hit);}
extern "C" int sm64ds_platform_displace(const DsWorldPlatformRef* platform,const int32_t* position,int32_t* result) {
    struct {DsWorldPlatformRef platform;int32_t position[3];} input{*platform,{position[0],position[1],position[2]}};
    return bridge&&bridge(Displace,&input,sizeof input,result,16)==1?1:0;
}
extern "C" int fixture_saved_open() {
    return bridge&&source.prepare()&&session.open(source.callbacks())?1:0;
}
extern "C" int fixture_saved_create(float x,float y,float z) {return session.create(x,y,z,nullptr);}
extern "C" int fixture_saved_step(int id,int x,int y,int jump,int attack,int crouch,float* output,uint32_t* action) {
    SM64MarioState state{};
    if(!session.tick_circle(id,x,y,0,jump!=0,attack!=0,crouch!=0,state))return 0;
    for(unsigned i=0;i<3;++i){output[i]=state.position[i];output[i+3]=state.velocity[i];}
    output[6]=state.faceAngle;output[7]=state.forwardVelocity;*action=state.action;return 1;
}
extern "C" unsigned fixture_saved_delegation() {return unsigned(source.delegation());}
extern "C" unsigned fixture_saved_faults() {return session.faults();}
extern "C" int fixture_saved_remove(int id) {return session.remove(id)?1:0;}
extern "C" int fixture_saved_reset_while_active() {return source.reset_after_session_closed()?1:0;}
extern "C" int fixture_saved_close() {session.close();return source.reset_after_session_closed()?1:0;}
