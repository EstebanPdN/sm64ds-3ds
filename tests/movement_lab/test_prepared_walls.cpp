#include "world_walls.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace movement_geometry;
static unsigned counts=0,reads=0,filters=0;
static int owner=-1;
extern "C" int sm64ds_world_prism_count(unsigned slot){++counts;return slot==0?1:0;}
extern "C" int sm64ds_world_prism_at(unsigned slot,unsigned index,DsWorldGroundHit* hit,DsWorldPrism* prism) {
    ++reads;if(slot||index!=1)return 0;
    *hit={};hit->normal[0]=-4096;hit->collider=0x1000;hit->owner_id=owner;hit->triangle=1;
    *prism={};prism->extent=100*65536;prism->anchor[0]=150*64;prism->anchor[2]=-50*64;
    const int16_t normals[4][3]={{-1024,0,0},{0,-1024,0},{0,0,1024},{0,1024,1024}};
    std::memcpy(prism->normal,normals,sizeof normals);prism->kcl=0x2000;prism->scale_y=4096;
    prism->transform[0]=prism->transform[4]=prism->transform[8]=4096;
    prism->world_anchor[0]=150*4096;prism->world_anchor[2]=-50*4096;return 1;
}
extern "C" int sm64ds_contact_pass_through(const DsWorldGroundHit*,void*,uint8_t,uint8_t){++filters;return 0;}
int main() {
    WorldWalls<2> cache;assert(cache.build()&&cache.size()==1);
    const DsWorldWallInput input{130,30,0,0,30};DsWorldWallResult output;
    std::memset(&output,0xcd,sizeof output);const auto saved=output;
    assert(cache.query_prepared(&input,nullptr,1,&output)==-1&&!std::memcmp(&saved,&output,sizeof saved));
    assert(cache.begin_tick());const auto reads_before=reads,counts_before=counts;
    for(unsigned step=0;step<8;++step) {
        assert(cache.query_prepared(&input,nullptr,1,&output)==1&&output.x==120&&output.z==0&&output.count==1);
        assert(reads==reads_before&&counts==counts_before);
    }
    assert(filters==8);cache.invalidate_tick();output=saved;
    assert(cache.query_prepared(&input,nullptr,1,&output)==-1&&!std::memcmp(&saved,&output,sizeof saved));
    owner=7;assert(!cache.begin_tick());
    assert(cache.query_prepared(&input,nullptr,1,&output)==-1&&!std::memcmp(&saved,&output,sizeof saved));
    assert(cache.build()&&cache.begin_tick()&&cache.query_prepared(&input,nullptr,1,&output)==1);
    cache.clear();output=saved;
    assert(cache.query_prepared(&input,nullptr,1,&output)==-1&&!std::memcmp(&saved,&output,sizeof saved));
    std::puts("Prepared walls: eight subqueries share one registry/geometry refresh; hits still filter; unprepared/invalidated/stale snapshots reject unchanged output");
}
