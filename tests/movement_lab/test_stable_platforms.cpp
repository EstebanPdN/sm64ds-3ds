#include "stable_platforms.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace movement_geometry;
static int mode=0,calls=0;
extern "C" int sm64ds_platform_displace(const DsWorldPlatformRef* owner,const int32_t position[3],int32_t result[4]) {
    ++calls;assert(owner->collider==0x1000&&owner->owner_id==7);
    if(mode==1)return 0;
    result[0]=position[0]+1024;result[1]=position[1]+2048;result[2]=position[2]-3072;
    result[3]=mode==2?99999:-16384;return 1;
}
int main() {
    StablePlatforms<2> storage;DsWorldPlatformRef a{0x1000,0x2000,0x3000,7,1,1},b=a,c=a;
    b.collider=0x1100;c.collider=0x1200;
    assert(!storage.acquire(a));assert(storage.begin_tick(nullptr,0));
    auto* first=storage.acquire(a);auto* second=storage.acquire(b);assert(first&&second&&first!=second);
    assert(storage.acquire(a)==first&&!storage.acquire(c));
    const SM64SurfaceObjectTransform* pins[]={first};assert(storage.begin_tick(pins,1));
    assert(storage.acquire(c)==second&&!storage.acquire(b));
    SM64SurfaceObjectTransform foreign{};const SM64SurfaceObjectTransform* invalid[]={&foreign};
    assert(!storage.begin_tick(invalid,1));assert(storage.acquire(a)==first);
    assert(storage.begin_tick(pins,1));auto replacement=a;replacement.owner_id=8;
    assert(storage.acquire(replacement)==second&&storage.acquire(a)==first&&!storage.acquire(b));
    float position[]={1.25f,2.5f,-3.75f};int16_t yaw=0;
    assert(storage.displace(first,position,&yaw)==1&&position[0]==1.5f&&position[1]==3.f&&position[2]==-4.5f&&yaw==-16384);
    const float saved[]={position[0],position[1],position[2]};
    for(mode=1;mode<=2;++mode) {
        yaw=123;assert(storage.displace(first,position,&yaw)==-1&&!std::memcmp(position,saved,sizeof saved)&&yaw==123);
    }
    const int before=calls;
    assert(storage.displace(&foreign,position,&yaw)==-1&&calls==before);
    position[0]=NAN;assert(storage.displace(first,position,&yaw)==-1&&calls==before);
    position[0]=1e20f;assert(storage.displace(first,position,&yaw)==-1&&calls==before);
    position[0]=saved[0];
    storage.reset_after_solver_deleted();assert(!storage.acquire(a));
    assert(storage.displace(first,position,&yaw)==-1&&calls==before);
    std::printf("Stable platform handles: retained/issued ownership, bounded reuse, original DS transport forwarding and rejected-output staging passed; owner bytes=%zu\n",sizeof storage);
}
