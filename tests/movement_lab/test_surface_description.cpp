#include "surface_description.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace movement_geometry;
struct Record {DsWorldPrism prism;DsWorldGroundHit hit;};
int main(int argc,char** argv) {
    assert(argc==2);FILE* f=std::fopen(argv[1],"rb");assert(f);uint32_t count;
    assert(std::fread(&count,4,1,f)==1&&count&&count<=32768);
    unsigned ready=0,water=0;Record seed{};
    for(unsigned i=0;i<count;++i) {
        Record record;assert(std::fread(&record,sizeof record,1,f)==1);
        SurfaceDescription description{},saved=description;
        auto status=describe_surface(record.hit,record.prism,nullptr,description);
        if(record.hit.clps[0]&32) {
            assert(status==SurfaceDescribe::Water&&!std::memcmp(&description,&saved,sizeof saved));++water;continue;
        }
        assert(status==SurfaceDescribe::Ready);++ready;seed=record;
        const auto& s=description.surface;
        assert(s.isValid && s.room==0 && !s.transform);
        assert(description.key.collider==record.hit.collider&&description.key.kcl==record.prism.kcl&&description.key.owner_id==uint32_t(record.hit.owner_id));
        assert(description.material.raw[0]==record.hit.clps[0]&&description.material.raw[1]==record.hit.clps[1]);
        assert(s.normal.x==record.hit.normal[0]/4096.f&&s.normal.y==record.hit.normal[1]/4096.f&&s.normal.z==record.hit.normal[2]/4096.f);
        const auto a=description.triangle.plane_anchor;
        const float expected=-(s.normal.x*float(a.x)+s.normal.y*float(a.y)+s.normal.z*float(a.z));
        assert(s.originOffset==expected);
        for(const auto& v:description.triangle.geometry.vertex)assert(s.lowerY<=v.y&&s.upperY>=v.y);
    }
    assert(std::fgetc(f)==EOF);std::fclose(f);assert(ready&&water);
    SurfaceDescription output{},saved=output;SM64SurfaceObjectTransform platform{};
    auto moving=seed;moving.prism.kind=1;
    assert(describe_surface(moving.hit,moving.prism,nullptr,output)==SurfaceDescribe::NeedsPlatform&&!std::memcmp(&saved,&output,sizeof saved));
    assert(describe_surface(moving.hit,moving.prism,&platform,output)==SurfaceDescribe::Ready);
    assert(output.surface.transform==&platform&&(output.surface.flags&SURFACE_FLAG_DYNAMIC));
    output=saved;
    assert(describe_surface(seed.hit,seed.prism,&platform,output)==SurfaceDescribe::InvalidContact&&!std::memcmp(&saved,&output,sizeof saved));
    for(unsigned effect:{1u,6u,7u,8u,9u,17u,20u}) {
        auto h=seed.hit;h.clps[0]=(h.clps[0]&~(31u<<19))|(effect<<19);
        assert(describe_surface(h,seed.prism,nullptr,output)==SurfaceDescribe::NeedsDsEffect&&!std::memcmp(&saved,&output,sizeof saved));
    }
    auto h=seed.hit;h.clps[0]=(h.clps[0]&~(7u<<12))|(7u<<12);
    assert(describe_surface(h,seed.prism,nullptr,output)==SurfaceDescribe::UnknownFriction&&!std::memcmp(&saved,&output,sizeof saved));
    h=seed.hit;h.triangle=0;
    assert(describe_surface(h,seed.prism,nullptr,output)==SurfaceDescribe::InvalidContact&&!std::memcmp(&saved,&output,sizeof saved));
    std::printf("Surface descriptions: %u ordinary captured contacts, %u excluded water contacts; original normals/planes/material metadata, platform requirement and rejected-output checks passed\n",ready,water);
}
