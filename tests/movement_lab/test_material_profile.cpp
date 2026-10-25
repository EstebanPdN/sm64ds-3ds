#include "material_profile.hpp"
#include <cassert>
#include <cstdio>
extern "C" int fixture_material_core(int,int,int,int);
extern "C" int func_ov002_020f035c(unsigned,int);
using namespace movement_geometry;
extern "C" int fixture_material_use(uint32_t first,uint32_t second) {return int(material_profile(first,second).use);}
extern "C" void fixture_material_fields(uint32_t first,uint32_t second,uint32_t* out) {
    const auto p=material_profile(first,second);
    const uint32_t values[]={p.floor_class,p.surface_effect,p.ground_sound,p.parameter,p.bits6_11,p.bits15_18,
        uint32_t(bool(first&0x20)),uint32_t(bool(p.filter_flags&1)),uint32_t(bool(p.filter_flags&2)),uint32_t(bool(p.filter_flags&4))};
    for(unsigned i=0;i<10;++i)out[i]=values[i];
}
int main() {
    unsigned cases=0;
    const int classes[]={SURFACE_CLASS_DEFAULT,SURFACE_CLASS_NOT_SLIPPERY,SURFACE_CLASS_NOT_SLIPPERY,SURFACE_CLASS_SLIPPERY,SURFACE_CLASS_VERY_SLIPPERY,SURFACE_CLASS_VERY_SLIPPERY};
    for(unsigned cls=0;cls<6;++cls) {
        auto profile=material_profile(cls<<12,0x123456ff);assert(profile.use==MaterialUse::Solid);
        assert(profile.raw[1]==0x123456ff && profile.class1_latch==(cls==1));
        SM64SurfaceCollisionData surface{};assert(apply_locomotion_material(profile,surface));
        assert(fixture_material_core(surface.type,surface.terrain,4096,0)==classes[cls]);
        for(int normal=0;normal<=4096;++normal) {
            assert(fixture_material_core(surface.type,surface.terrain,normal,1)==func_ov002_020f035c(cls,normal));++cases;
        }
    }
    for(unsigned cls=0;cls<8;++cls)for(unsigned effect=0;effect<32;++effect) {
        auto p=material_profile((cls<<12)|(effect<<19),0x765432ff);
        SM64SurfaceCollisionData surface{};surface.type=99;surface.terrain=88;surface.force=77;
        const bool mapped=apply_locomotion_material(p,surface);
        assert(mapped==(cls<6 && effect==0));
        if(!mapped)assert(surface.type==99&&surface.terrain==88&&surface.force==77);
        assert(p.raw[1]==0x765432ff && p.surface_effect==effect);
        assert(material_profile((cls<<12)|(effect<<19)|0x20,0).use==MaterialUse::Water);
    }
    assert(material_profile(0x02000000,0).use==MaterialUse::NeedsDsEffect);
    std::printf("materials: %u DS-vs-N64 slope cases, six friction classes and explicit special-surface rejection passed\n",cases);
}
