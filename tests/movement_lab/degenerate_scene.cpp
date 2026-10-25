#include "world_walls.hpp"
#include <cstring>
using namespace movement_geometry;
using Bridge=int(*)(unsigned,const void*,unsigned,void*,unsigned);
static Bridge bridge=nullptr;static WorldWalls<1024> walls;
extern "C" void fixture_bridge(Bridge b){bridge=b;}
extern "C" int sm64ds_world_prism_count(unsigned slot){return bridge?bridge(1,&slot,4,nullptr,0):-1;}
extern "C" int sm64ds_world_prism_at(unsigned slot,unsigned tri,DsWorldGroundHit* hit,DsWorldPrism* prism){
 const unsigned in[]={slot,tri};struct {DsWorldGroundHit hit;DsWorldPrism prism;} out;
 if(!bridge||bridge(2,in,8,&out,sizeof out)!=1)return 0;*hit=out.hit;*prism=out.prism;return 1;
}
extern "C" int sm64ds_contact_pass_through(const DsWorldGroundHit* hit,void* actor,uint8_t flags,uint8_t wall){
 struct {DsWorldGroundHit hit;uint32_t actor;uint8_t flags,wall,pad[2];} in{*hit,uint32_t(uintptr_t(actor)),flags,wall,{0,0}};return bridge?bridge(3,&in,sizeof in,nullptr,0):-1;
}
extern "C" int fixture_build(){return walls.build();}
extern "C" unsigned fixture_size(){return unsigned(walls.size());}
extern "C" int fixture_decode(const DsWorldPrism* p,const int32_t* normal,double* out){
 WorldTriangle t;const auto status=decode_world(*p,normal,t);if(status!=WorldDecode::Ok&&status!=WorldDecode::Degenerate)return int(status);
 unsigned k=0;for(const auto& v:t.geometry.vertex){out[k++]=v.x;out[k++]=v.y;out[k++]=v.z;}return int(status);
}
extern "C" int fixture_contact(const DsWorldPrism* p,const int32_t* normal,const float* point){
 WorldTriangle t;const auto status=decode_world(*p,normal,t);if(status!=WorldDecode::Ok&&status!=WorldDecode::Degenerate)return -99;
 WallPush untouched{12345,67890};const auto result=wall_contact(t,{point[0],point[1],point[2],0,50},untouched);
 if(result!=WallContact::Hit&&(untouched.x!=12345||untouched.z!=67890))return -98;return int(result);
}
