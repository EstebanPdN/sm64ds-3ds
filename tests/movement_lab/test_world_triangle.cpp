#include "world_triangle.hpp"
#include <cassert>
#include <cstdio>
using namespace movement_geometry;
int main(int argc,char** argv) {
    assert(argc==2);FILE* f=std::fopen(argv[1],"rb");assert(f);
    uint32_t count;assert(std::fread(&count,4,1,f)==1 && count && count<=32768);
    for(unsigned i=0;i<count;++i) {
        DsWorldPrism packet;int32_t normal[3];
        assert(std::fread(&packet,sizeof packet,1,f)==1 && std::fread(normal,sizeof normal,1,f)==1);
        WorldTriangle world;assert(decode_world(packet,normal,world)==WorldDecode::Ok);
        // Independent scalar reference, preserving the decoder's canonical
        // local ordering; accept the deliberate final winding swap.
        Prism p{};p.extent=packet.extent;p.attribute=packet.attribute;
        std::memcpy(p.anchor,packet.anchor,12);std::memcpy(p.normal,packet.normal,24);
        Triangle local;assert(decode(p,local)==Decode::Ok);
        for(unsigned vertex=0;vertex<3;++vertex) {
            const Vec3 v=local.vertex[vertex];const auto* m=packet.transform;
            const double x=v.x,y=v.y*packet.scale_y/4096.,z=v.z;
            Vec3 expected{(m[0]*x+m[3]*y+m[6]*z+m[9])/4096.,
                          (m[1]*x+m[4]*y+m[7]*z+m[10])/4096.,
                          (m[2]*x+m[5]*y+m[8]*z+m[11])/4096.};
            bool found=false;for(const auto& actual:world.geometry.vertex)
                if(dot(subtract(expected,actual),subtract(expected,actual))<1e-12)found=true;
            assert(found);
        }
        assert(world.geometry.normal.x==normal[0]/4096. && world.geometry.normal.y==normal[1]/4096. && world.geometry.normal.z==normal[2]/4096.);
        const auto saved=world;
        auto invalid=packet;invalid.kind=3;
        assert(decode_world(invalid,normal,world)==WorldDecode::UnsupportedKind && !std::memcmp(&saved,&world,sizeof world));
        invalid=packet;invalid.scale_y=0;assert(decode_world(invalid,normal,world)==WorldDecode::InvalidScale);
        invalid=packet;std::memset(invalid.transform,0,sizeof invalid.transform);
        assert(decode_world(invalid,normal,world)==WorldDecode::InvalidTransform);
        int32_t zero[3]={};assert(decode_world(packet,zero,world)==WorldDecode::InvalidNormal);
        assert(!std::memcmp(&saved,&world,sizeof world));
    }
    assert(std::fgetc(f)==EOF);std::fclose(f);
    std::printf("World triangle decode: %u captured/controlled records; fractional transforms, normals, winding and rejected-output checks passed\n",count);
}
