#include "sm64ds/game_collision.hpp"
extern "C" int native_collision_init(void* s,void* f,void* c) {
    return sm64ds::collision_initialize(s,f,c);
}
extern "C" int native_collision_ground(void* s,const int32_t* p,int32_t lower,void* out) {
    return sm64ds::collision_ground(s,p,lower,*static_cast<sm64ds::GroundHit*>(out));
}
extern "C" void native_collision_destroy(void* s) { sm64ds::collision_destroy(s); }
extern "C" int native_collision_sphere(void* s,const int32_t* p,int32_t r,void* out) {
    return sm64ds::collision_sphere(s,p,r,*static_cast<sm64ds::SphereHit*>(out));
}
extern "C" int native_collision_line(void* s,const int32_t* a,const int32_t* b,int32_t* out) {
    return sm64ds::collision_line(s,a,b,out);
}
