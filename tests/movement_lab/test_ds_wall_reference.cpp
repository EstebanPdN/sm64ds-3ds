// Host counterpart to the original-ARM saved-registry replay. Allocations and
// file I/O belong only to this fixture, never to the wall query implementation.
#include "wall_contact.hpp"
#include "world_walls.hpp"
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace movement_geometry;
struct Record {DsWorldPrism prism;DsWorldGroundHit hit;};
static std::vector<Record> records;
static bool fail_later=false;
extern "C" int sm64ds_world_prism_count(unsigned slot) {
    if(fail_later&&slot==23)return -1;
    int count=0;for(const auto& r:records)if(r.hit.slot==slot)count=std::max(count,int(r.hit.triangle));return count;
}
extern "C" int sm64ds_world_prism_at(unsigned slot,unsigned triangle,DsWorldGroundHit* hit,DsWorldPrism* prism) {
    for(const auto& r:records)if(r.hit.slot==slot&&r.hit.triangle==triangle){*hit=r.hit;*prism=r.prism;return 1;}
    return 0;
}
extern "C" int sm64ds_contact_pass_through(const DsWorldGroundHit* hit,void*,uint8_t mask,uint8_t wall) {
    const auto word=hit->clps[0];bool special=false;
    if(word&32){special=true;if(!(mask&2))return 1;}
    if(word&(1<<25)){special=true;if(!(mask&8))return 1;}
    if(word&(1<<24)){special=true;if(mask&4)return 1;}
    if(word&(1<<26)){special=true;if(!(mask&4))return 1;}
    if(((word>>19)&31)==17){special=true;if(mask&32)return 1;}
    if(wall&&((word>>19)&31)==20){special=true;if(mask&64)return 1;}
    return !special&&!(mask&1);
}
struct Case {DsWorldWallInput input;int32_t status;DsWorldWallResult result;};
int main(int argc,char** argv) {
    assert(argc==3);FILE* f=std::fopen(argv[1],"rb");assert(f);uint32_t count;
    assert(std::fread(&count,4,1,f)==1&&count<=32768);records.resize(count);
    assert(std::fread(records.data(),sizeof(Record),count,f)==count&&std::fgetc(f)==EOF);std::fclose(f);
    std::vector<Case> cases;unsigned walls=0;
    static WorldWalls<1024> cache;assert(cache.build());const auto entries=cache.size();
    assert(cache.prism_reads()==records.size());
    for(const auto& record:records) {
        if(std::abs(record.hit.normal[1])>40)continue;
        if(walls++%50)continue;
        WorldTriangle triangle;assert(decode_world(record.prism,record.hit.normal,triangle)==WorldDecode::Ok);
        Vec3 center{};for(auto v:triangle.geometry.vertex){center.x+=v.x/3;center.y+=v.y/3;center.z+=v.z/3;}
        const auto n=triangle.geometry.normal;
        Case sample{};sample.input={float(center.x+n.x*15),float(center.y),float(center.z+n.z*15),0,30};
        sample.status=sm64ds_world_walls_reference(&sample.input,nullptr,1,&sample.result);
        assert(sample.status==1&&sample.result.count>0&&sample.result.stored<=4);
        DsWorldWallResult cached{};
        assert(cache.query(&sample.input,nullptr,1,&cached)==sample.status && !std::memcmp(&cached,&sample.result,sizeof cached));
        assert(cache.prism_reads()<records.size()/10);
        cases.push_back(sample);
    }
    Case missing{};missing.input={100000,100000,100000,0,30};
    std::memset(&missing.result,0xcd,sizeof missing.result);auto saved=missing.result;
    missing.status=sm64ds_world_walls_reference(&missing.input,nullptr,1,&missing.result);
    assert(!missing.status&&!std::memcmp(&saved,&missing.result,sizeof saved));cases.push_back(missing);
    fail_later=true;DsWorldWallResult output=saved;
    assert(sm64ds_world_walls_reference(&cases.front().input,nullptr,1,&output)==-1&&!std::memcmp(&saved,&output,sizeof saved));
    assert(cache.query(&cases.front().input,nullptr,1,&output)==-1&&!std::memcmp(&saved,&output,sizeof saved));
    fail_later=false;
    ++records.front().hit.owner_id;
    assert(cache.query(&cases.front().input,nullptr,1,&output)==-1&&!std::memcmp(&saved,&output,sizeof saved));
    --records.front().hit.owner_id;
    static WorldWalls<1> too_small;assert(!too_small.build());
    assert(too_small.query(&cases.front().input,nullptr,1,&output)==-1&&!std::memcmp(&saved,&output,sizeof saved));
    for(auto radius:{-1.f,NAN}) {
        auto input=cases.front().input;input.radius=radius;
        assert(sm64ds_world_walls_reference(&input,nullptr,1,&output)==-1&&!std::memcmp(&saved,&output,sizeof saved));
    }
    f=std::fopen(argv[2],"wb");assert(f);count=cases.size();
    assert(std::fwrite(&count,4,1,f)==1&&std::fwrite(cases.data(),sizeof(Case),count,f)==count);assert(!std::fclose(f));
    std::printf("DS wall reference/cache: %u wall candidates, %zu cache entries, %u registry queries; identical cached output, bounded reads, stale/fault/capacity staging passed\n",walls,entries,count);
}
