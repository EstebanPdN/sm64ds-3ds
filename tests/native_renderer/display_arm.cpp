// ROM-free32-bit differential against pinned original display producers.
#include "menu_display_state.hpp"
#include <cstring>
using namespace sm64ds::render;
namespace hooks=sm64ds::render::game_hooks;
struct ReferenceG2x {
    static void SetBlendBrightness(volatile unsigned short*,unsigned short,short);
    static void SetBlendAlpha(volatile unsigned short*,unsigned short,unsigned short,unsigned short,unsigned int);
};
namespace {
bool enabled=false;unsigned forwarded=0,transfers=0;unsigned rng=0x44535059;
std::uint64_t ticks=0;std::uint64_t clock(){return ++ticks;}
alignas(4) unsigned char global[0x78]{},owner[0x48]{},bg[8][0x38]{},ref_bg[0x38]{},effect[0x34]{},ref_effect[0x34]{};
void* backgrounds[8]{};
unsigned random(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
template<class T>void set(void* p,unsigned at,T v){std::memcpy(static_cast<char*>(p)+at,&v,sizeof(v));}
template<class T>T get(const void* p,unsigned at){T v;std::memcpy(&v,static_cast<const char*>(p)+at,sizeof(v));return v;}
}
namespace sm64ds::render::game_hooks {bool native_active() noexcept{return enabled;}}
// Background adapter coverage has its own original-ARM differential. Keep
// this helper/MMIO differential isolated from that separately tested owner.
namespace sm64ds::render::game_hooks {
void menu_background_group_begin(unsigned) noexcept {}
void menu_background_identity(const void*,const void*,unsigned,unsigned) noexcept {}
void menu_background_publish(unsigned,const void*,unsigned,unsigned,unsigned,int,int) noexcept {}
}
extern "C" {
void* data_ov007_0210342c=global;
volatile unsigned display_native_interval=0;
void reference_strength(void*,int);
void reference_disable(void*);
void reference_background(void*,int);
void __wrap__ZN3G2x18SetBlendBrightnessEPVtts(void*,std::uint16_t,std::int16_t);
void __wrap__ZN3G2x13SetBlendAlphaEPVttttj(void*,std::uint16_t,std::uint16_t,std::uint16_t,std::uint32_t);
void __wrap_func_020551f0(void*,int);
void __wrap_func_ov007_020c897c(void*);
void __wrap_func_ov007_020c076c(void*,int);
void __wrap_func_ov007_020c03b0(void*);
void __real__ZN3G2x18SetBlendBrightnessEPVtts(void* p,std::uint16_t m,std::int16_t a){++forwarded;ReferenceG2x::SetBlendBrightness(static_cast<volatile unsigned short*>(p),m,a);}
void __real__ZN3G2x13SetBlendAlphaEPVttttj(void* p,std::uint16_t a,std::uint16_t b,std::uint16_t c,std::uint32_t d){++forwarded;ReferenceG2x::SetBlendAlpha(static_cast<volatile unsigned short*>(p),a,b,c,d);}
void __real_func_020551f0(void* p,int a){++forwarded;reference_strength(p,a);}
void __real_func_ov007_020c897c(void* p){++forwarded;reference_disable(p);}
void __real_func_ov007_020c03b0(void*){}
void __real_func_ov007_020c076c(void* p,int a){++forwarded;reference_background(p,a);}
void _ZN4CP1527FlushAndInvalidateDataCacheEjj(unsigned,unsigned){}
void func_02056554(const void*,int,int){++transfers;}
void func_02056494(const void*,int,int){++transfers;}
void func_020563d4(const void*,unsigned,unsigned){++transfers;}
void func_02056314(void*,unsigned,unsigned){++transfers;}
void func_020564f4(const void*,int,int){++transfers;}
void func_02056434(const void*,int,int){++transfers;}
void func_02056374(const void*,unsigned,unsigned){++transfers;}
void func_020562b4(const void*,unsigned,unsigned){++transfers;}
}
#define REQUIRE(v) do {if(!(v))return __LINE__;} while(0)
extern "C" int native_display_regression(){
    set(global,0x74,static_cast<void*>(owner));set(owner,0x34,static_cast<void*>(backgrounds));
    for(unsigned i=0;i<8;++i)backgrounds[i]=bg[i];
    enabled=true;hooks::menu_display_scene_changed();
    for(unsigned i=0;i<4096;++i){const unsigned engine=i&1,targets=random()&63;const auto amount=std::int16_t(int(random()%63)-31);
        auto* port=reinterpret_cast<volatile unsigned short*>(engine?0x04001050:0x04000050);
        ReferenceG2x::SetBlendBrightness(port,targets,amount);
        display_native_interval=1;__wrap__ZN3G2x18SetBlendBrightnessEPVtts(const_cast<unsigned short*>(port),targets,amount);display_native_interval=0;
        auto s=hooks::menu_display_snapshot().effect[engine];REQUIRE(s.known&&s.mode==hooks::DisplayEffect::brightness&&s.first==(port[0]&63));
        REQUIRE(s.amount==(port[0]&0x40?-int(port[2]>16?16:port[2]):int(port[2]>16?16:port[2])));
        const int changed=int(random()%63)-31;reference_strength(const_cast<unsigned short*>(port),changed);
        display_native_interval=1;__wrap_func_020551f0(const_cast<unsigned short*>(port),changed);display_native_interval=0;
        s=hooks::menu_display_snapshot().effect[engine];REQUIRE(s.amount==(port[0]&0x40?-int(port[2]>16?16:port[2]):int(port[2]>16?16:port[2])));
        const unsigned second=random()&63,a=random()%32,b=random()%32;
        ReferenceG2x::SetBlendAlpha(port,targets,second,a,b);
        display_native_interval=1;__wrap__ZN3G2x13SetBlendAlphaEPVttttj(const_cast<unsigned short*>(port),targets,second,a,b);display_native_interval=0;
        s=hooks::menu_display_snapshot().effect[engine];REQUIRE(s.mode==hooks::DisplayEffect::alpha&&s.first==(port[0]&63)&&s.second==(port[0]>>8));
        REQUIRE(s.alpha_a==((port[1]&31)>16?16:(port[1]&31))&&s.alpha_b==((port[1]>>8)>16?16:(port[1]>>8)));
        // The original disable mutates animation counters. Native route must
        // preserve all bytes outside its three exact source state fields.
        std::memset(effect,0x51,sizeof(effect));set<int>(effect,0,2+int(random()%3));set<unsigned>(effect,4,1+random()%3);std::memcpy(ref_effect,effect,sizeof(effect));reference_disable(ref_effect);
        display_native_interval=1;__wrap_func_ov007_020c897c(effect);display_native_interval=0;REQUIRE(!std::memcmp(effect,ref_effect,sizeof(effect)));
    }
    REQUIRE(forwarded==0&&!hooks::menu_display_snapshot().failure);
    hooks::menu_display_clock(clock);
    for(unsigned i=0;i<2048;++i){unsigned group_flags=3;__wrap_func_ov007_020c03b0(&group_flags);const unsigned id=i%8,layer=id%4,flags=1+random()%3;auto* p=bg[id];std::memset(p,0,sizeof(bg[0]));
        set<int>(p,0,int(layer));set<int>(p,4,int(random()%4));set<int>(p,0xc,int(random()&1));
        set<std::uintptr_t>(p,0x10,(random()&3)?0x01234560:0);set<std::uint16_t>(p,0x1c,32);set<std::uint16_t>(p,0x1e,24);
        for(unsigned at:{0x20u,0x24u,0x28u,0x2cu})set<std::int32_t>(p,at,int(random()%4000001)-2000000);
        std::memcpy(ref_bg,p,sizeof(ref_bg));reference_background(ref_bg,flags);
        const auto prior=transfers;display_native_interval=1;__wrap_func_ov007_020c076c(p,int(flags));display_native_interval=0;
        REQUIRE(transfers==prior&&!std::memcmp(p,ref_bg,sizeof(ref_bg)));
        for(unsigned e=0;e<2;++e)if(flags&(1u<<e)){const auto& out=hooks::menu_display_snapshot().background[e*4+layer];
            REQUIRE(out.seen&&out.resource==id&&out.width==32&&out.height==24);
            if(out.has_content){const auto* scroll=reinterpret_cast<volatile unsigned*>(0x04000010+e*0x1000+layer*4);
                REQUIRE((unsigned(out.x)&511)==(*scroll&511)&&(unsigned(out.y)&511)==((*scroll>>16)&511));}
        }
    }
    REQUIRE(forwarded==0&&!hooks::menu_display_snapshot().failure);
    REQUIRE(hooks::menu_display_statistics().background_group_ticks==2048&&ticks==4096);hooks::menu_display_clock(nullptr);
    // Inactive integration must forward the original producer exactly once.
    enabled=false;auto* port=reinterpret_cast<void*>(0x04000050);
    __wrap__ZN3G2x18SetBlendBrightnessEPVtts(port,63,-8);__wrap_func_020551f0(port,4);
    __wrap__ZN3G2x13SetBlendAlphaEPVttttj(port,1,32,8,8);__wrap_func_ov007_020c897c(effect);__wrap_func_ov007_020c076c(bg[0],1);
    REQUIRE(forwarded==5);return 0;
}
