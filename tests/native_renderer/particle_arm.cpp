// Original32-bit source producer differential; ROM-free authored objects.
// Pinned original be40/bbd8 compute the reference pose/polygon/UV arguments.
#include "particle_hooks.hpp"
#include <cstring>
using namespace sm64ds::render;
namespace hooks=sm64ds::render::game_hooks;
namespace {
alignas(4) unsigned char manager[0x3c]{},system_object[0x78]{},definition[0x38]{},resource[0x20]{},
    child_definition[0x14]{},node[0x44]{},descriptors[20]{},menu_file[32]{},tracker[12]{};
game::Affine view=game::unit_affine();
ImageAsset asset{16,16,ImageBank::Alpha::palette_zero_mask};
ImageBankBinding bindings[]={{0,{},&asset,1},{1,{},&asset,1}};
bool enabled=false,tracking=false,emit_child=false,empty_dispatch=false;
unsigned faults=0,forwarded=0,loads=0,dispatches=0,draws=0;
unsigned tex_s=0,tex_t=0;std::int32_t reference_pose[12]{};
hooks::ParticleDraw saved;
std::uint64_t ticks=0;
template<class T>void set(void* object,unsigned at,T value){std::memcpy(static_cast<char*>(object)+at,&value,sizeof(value));}
template<class T>T get(const void* object,unsigned at){T value;std::memcpy(&value,static_cast<const char*>(object)+at,sizeof(value));return value;}
std::uint64_t clock(){return ++ticks;}
bool capture(const hooks::ParticleDraw& draw,void* user){if(user!=menu_file)return false;saved=draw;++draws;return true;}
unsigned random_state=0x70617274;
unsigned random(){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
}
namespace sm64ds::render::game_hooks {
bool tracking_enabled() noexcept {return tracking;}
bool native_active() noexcept {return enabled;}
void report_image_failure() noexcept {++faults;}
}
extern "C" {
std::int16_t data_02082214[8192]{};
unsigned char data_02075f14[32]{};
void* data_ov007_0210344c=manager;
unsigned char data_0209d464=0;
volatile unsigned particle_native_interval=0;
int flush_enabled=1;
int func_02019018(){return flush_enabled;}
void* __real_func_0204a4c8(void* (*)(int),int,int,int,unsigned short){++forwarded;return manager;}
void __real_func_0204a17c(void* object,void*){
    ++loads;set(object,0x20,static_cast<void*>(descriptors));set<std::uint16_t>(object,0x26,1);
}
void __real_func_0204af3c(void*){++forwarded;}
void __real_func_0204be40(void*,void*){++forwarded;}
void __real_func_0204bbd8(void*,void*){++forwarded;}
void __real_func_020190b8(){++forwarded;}
void __real_func_ov007_020b7764(){++forwarded;}
void __real__ZN8Particle10SysTrackerD1Ev(void*){++forwarded;}
void __wrap_func_0204a5c8(void*);
void __wrap_func_0204af3c(void*);
void __wrap_func_0204be40(void*,void*);
void __wrap_func_0204bbd8(void*,void*);
void __wrap_func_0204a17c(void*,void*);
void* __wrap_func_0204a4c8(void* (*)(int),int,int,int,unsigned short);
void __wrap_func_ov007_020b7764();
void __wrap__ZN8Particle10SysTrackerD1Ev(void*);
void __wrap_func_020190b8();
void __real_func_0204a5c8(void* object){
    ++dispatches;__wrap_func_0204af3c(descriptors);
    if(!empty_dispatch){if(emit_child)__wrap_func_0204bbd8(object,node);else __wrap_func_0204be40(object,node);}
}
void func_02055388(std::int32_t* matrix){std::memcpy(reference_pose,matrix,48);}
void func_0204c24c(unsigned char s,unsigned char t){tex_s=s;tex_t=t;}
void reference_billboard(void*,void*);
void reference_child_billboard(void*,void*);
int native_particle_regression(){
#define REQUIRE(condition) do{if(!(condition))return __LINE__;}while(false)
    view=game::unit_affine();
    __wrap_func_0204be40(manager,node);__wrap_func_0204bbd8(manager,node);__wrap_func_020190b8();
    REQUIRE(forwarded==3&&!faults);
    ImageCatalog catalog;REQUIRE(ImageCatalog::open(bindings,2,catalog)==Error::none);
    REQUIRE(hooks::install_particles(catalog,capture,menu_file));tracking=enabled=true;
    REQUIRE(__wrap_func_0204a4c8(nullptr,1,2,3,4)==manager&&forwarded==4);
    hooks::particle_file_loaded(menu_file,1);__wrap_func_0204a17c(manager,menu_file);
    REQUIRE(loads==1&&hooks::particle_statistics().manager_loads==1);
    set(manager,0x34,static_cast<void*>(system_object));set(manager,0x38,static_cast<void*>(&view));
    set(system_object,0x18,static_cast<void*>(resource));set(resource,0,static_cast<void*>(definition));
    set(resource,0x14,static_cast<void*>(child_definition));
    set<std::uint32_t>(descriptors,12,2u|(1u<<4)|(1u<<8)|(3u<<12)|(1u<<14)|(1u<<16));
    set<std::uint16_t>(descriptors,16,16);set<std::uint16_t>(descriptors,18,16);
    // Source records/pixels disappear after upload: copied manager identity
    // remains usable, while a released manager itself must become unusable.
    hooks::particle_file_retiring(menu_file);std::memset(menu_file,0xcd,sizeof(menu_file));
    // Independent original arithmetic over authored cases, both billboard
    // families, all six-bit counter boundaries and alpha0 mutation.
    for(unsigned trial=0;trial<2048;++trial){
        emit_child=bool(trial&1);const auto angle=random()&65535;const auto table_index=angle>>4;
        data_02082214[table_index*2]=std::int16_t(int(random()%8193)-4096);
        data_02082214[table_index*2+1]=std::int16_t(int(random()%8193)-4096);
        set<std::uint16_t>(node,0x36,std::uint16_t(angle));
        set<std::int16_t>(definition,0x20,std::int16_t(int(random()%32768)-16384));
        set<std::int32_t>(node,0x30,std::int32_t(random()));set<std::int16_t>(node,0x34,std::int16_t(random()));
        set<std::uint16_t>(node,0x3a,std::uint16_t(random()&32767));
        set<std::uint16_t>(node,0x40,std::uint16_t(random()&1023));
        const unsigned s=random()&3,t=random()&3;
        set<std::uint32_t>(definition,0x34,(s<<24)|(t<<26));set<std::uint32_t>(child_definition,0x10,s|(t<<2));
        for(unsigned i=0;i<3;++i){
            set<std::int32_t>(node,8+i*4,std::int32_t(random()&65535)-32768);
            set<std::int32_t>(node,0x14+i*4,std::int32_t(random()&0xfffff)-0x80000);
            view.value[9+i]=std::int32_t(random()&65535)-32768;
        }
        view.value[0]=3072;view.value[1]=512;view.value[3]=-1024;view.value[4]=4096;
        const auto counter=random();set(system_object,0x74,counter);set<std::uint32_t>(manager,0x30,(trial&2)?0x8000:0);
        std::memset(reference_pose,0,sizeof(reference_pose));tex_s=tex_t=99;
        if(emit_child)reference_child_billboard(manager,node);else reference_billboard(manager,node);
        const auto reference_counter=get<std::uint32_t>(system_object,0x74);
        const auto reference_polygon=*reinterpret_cast<volatile const unsigned*>(0x040004a4);
        set(system_object,0x74,counter);const auto before=draws;
        particle_native_interval=1;__wrap_func_0204a5c8(manager);particle_native_interval=0;
        REQUIRE(get<std::uint32_t>(system_object,0x74)==reference_counter&&!faults);
        const auto opacity=get<std::uint16_t>(node,0x40);const unsigned alpha=((opacity&31)*(((opacity>>5)&31)+1))>>5;
        REQUIRE(draws==before+(alpha!=0));
        if(alpha){
            REQUIRE(!std::memcmp(saved.geometry.object.value,reference_pose,48));
            REQUIRE(saved.bank_id==1&&saved.image==0&&saved.polygon_id==((reference_polygon>>24)&63));
            REQUIRE(saved.fog==bool(reference_polygon&0x8000)&&tex_s==s&&tex_t==t);
            REQUIRE(saved.geometry.vertex[0].x==-1&&saved.geometry.vertex[0].y==1&&saved.geometry.vertex[2].x==1&&saved.geometry.vertex[2].y==-1);
            REQUIRE(saved.geometry.vertex[1].u==float(1u<<s)&&saved.geometry.vertex[2].v==float(1u<<t));
            REQUIRE(saved.geometry.vertex[0].a==alpha*255/31&&saved.material.sample_alpha);
            REQUIRE(saved.material.wrap_s==Wrap::mirror&&saved.material.wrap_t==Wrap::repeat);
        }
    }
    // Exactly count63 increments to0 before limit comparison, even if start
    // is nonzero; invisible particles must still consume this count.
    set<std::uint32_t>(system_object,0x74,7u|(63u<<6)|(63u<<12));set<std::uint16_t>(node,0x40,0);
    const auto before=draws;__wrap_func_0204a5c8(manager);
    REQUIRE(draws==before&&get<std::uint32_t>(system_object,0x74)==(7u|(63u<<6))&&!faults);
    set<std::uint16_t>(node,0x40,1023);set<std::uint32_t>(system_object,0x74,63u<<6);
    hooks::particle_clock(clock);__wrap_func_0204a5c8(manager);REQUIRE(ticks==2&&hooks::particle_statistics().capture_ticks==1);
    hooks::particle_clock(nullptr);__wrap_func_0204a5c8(manager);REQUIRE(ticks==2);
    set<std::uint32_t>(descriptors,12,2u|(1u<<4)|(1u<<8));__wrap_func_0204a5c8(manager);REQUIRE(!saved.material.sample_alpha);
    // Signed16 UV wrap from the original width scale, not normalized repeat.
    set<std::uint16_t>(descriptors,16,1024);set<std::uint32_t>(definition,0x34,3u<<24);emit_child=false;
    __wrap_func_0204a5c8(manager);REQUIRE(saved.geometry.vertex[1].u==0&&!faults);
    empty_dispatch=true;hooks::particle_manager_retiring(manager);__wrap_func_0204a5c8(manager);REQUIRE(!faults);
    empty_dispatch=false;__wrap_func_0204a5c8(manager);REQUIRE(faults==1);
    // Core manager uses the reviewed embedded source identity, not its pixels.
    __wrap_func_0204a17c(manager,data_02075f14);__wrap_func_0204a5c8(manager);REQUIRE(saved.bank_id==0&&faults==1);
    set(tracker,4,static_cast<void*>(manager));__wrap__ZN8Particle10SysTrackerD1Ev(tracker);
    __wrap_func_0204a5c8(manager);REQUIRE(faults==2);
    hooks::particle_file_loaded(menu_file,1);__wrap_func_0204a17c(manager,menu_file);
    __wrap_func_ov007_020b7764();__wrap_func_0204a5c8(manager);REQUIRE(faults==3);
    // Reused manager address cannot inherit the prior association.
    __wrap_func_0204a17c(manager,menu_file);__wrap_func_0204a4c8(nullptr,1,2,3,4);
    __wrap_func_0204a5c8(manager);REQUIRE(faults==4);
    flush_enabled=0;__wrap_func_020190b8();REQUIRE(!data_0209d464&&!hooks::particle_statistics().flushes);
    flush_enabled=1;__wrap_func_020190b8();REQUIRE(data_0209d464==1&&hooks::particle_statistics().flushes==1);
    enabled=false;const auto original=forwarded;__wrap_func_0204be40(manager,node);__wrap_func_0204bbd8(manager,node);__wrap_func_020190b8();
    REQUIRE(forwarded==original+3);
    return 0;
}
}
