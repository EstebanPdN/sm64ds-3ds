// Exercise the real32-bit descriptor/sprite hook, with authored game objects.
// Original CPU rotation is a seam; its arguments and produced basis are checked.
#include "menu_sprite_hooks.hpp"
#include "model_components_hooks.hpp"
#include <cstring>
using namespace sm64ds::render;
namespace hooks=sm64ds::render::game_hooks;
namespace {
alignas(4) unsigned char object[0x88]{},descriptor[0x2c]{},file[64]{},file2[64]{};
const void* descriptor_table[1]={descriptor};
ImageBinding image{0,0,0,64,64,ImageUse::sprite};
MaterialBinding material{};PartBinding part{};
ModelBindings bindings[]={{222,{},1,1,&image,1,&material,1,&part,1},{226,{},1,1,&image,1,&material,1,&part,1}};
unsigned original=0,faults=0,draws=0;
unsigned refreshes=0,destroyed=0;
int rotation[3]{};
unsigned rotation_calls=0;
std::uint64_t ticks=0;
std::uint64_t clock(){return ++ticks;}
hooks::MenuSpriteDraw saved[2];
template<class T>void set(unsigned offset,T value){std::memcpy(object+offset,&value,sizeof(value));}
bool capture(const hooks::MenuSpriteDraw& draw,void* user){
    if(user!=file||draws>=2)return false;
    saved[draws++]=draw;return true;
}
bool unused_model(const ModelBindings&,const ::ModelComponents&,const ::Matrix4x3*,const ::Vector3*,void*){return false;}
}
struct BMD_File;
namespace sm64ds::render::game_hooks {
void initialize_image_tracking() noexcept {}
void retire_image_tracking(const ::BMD_File*) noexcept {}
}
extern "C" {
std::int32_t data_ov007_0210325c=4096;
void* __real_func_ov007_020b2308(void* p,int index){if(p!=file||index!=0)++faults;return descriptor;}
void __real_func_ov007_020b22e0(void* d,void* p,int index){if(d!=descriptor||p!=file2||index!=0)++faults;++refreshes;}
int __real_func_ov007_020c1620(void* p){
    if(p!=descriptor)++faults;
    for(unsigned i=0;i<64;++i){hooks::MenuImageDiagnostic d;if(hooks::menu_image_diagnostic(i,d)&&d.descriptor==reinterpret_cast<std::uintptr_t>(p))++faults;}
    ++destroyed;return 0x1234;
}
void __real_func_ov007_020c421c(void* p){if(p!=object)++faults;++original;}
void func_ov007_020c3abc(std::int32_t* m,int x,int y,int z){
    ++rotation_calls;
    rotation[0]=x;rotation[1]=y;rotation[2]=z;
    for(unsigned i=0;i<9;++i)m[i]=(i%4==0)?4096:0;
}
void* __wrap_func_ov007_020b2308(void*,int);
void __wrap_func_ov007_020b22e0(void*,void*,int);
int __wrap_func_ov007_020c1620(void*);
void __wrap_func_ov007_020c421c(void*);
int native_menu_sprite_regression(){
#define REQUIRE(condition) do{if(!(condition))return __LINE__;}while(false)
    __wrap_func_ov007_020c421c(object);REQUIRE(original==1&&!faults);
    std::uint32_t value=4;std::memcpy(descriptor+4,&value,4);value=3;
    std::memcpy(descriptor+0x14,&value,4);std::memcpy(descriptor+0x18,&value,4);
    ModelCatalog catalog;REQUIRE(ModelCatalog::open(bindings,2,catalog)==Error::none);
    REQUIRE(hooks::install(catalog,unused_model,nullptr)&&hooks::activate(true));hooks::direct_model_loaded(222,file);
    REQUIRE(hooks::install_menu_sprites(capture,file));
    hooks::menu_sprite_clock(clock);
    REQUIRE(__wrap_func_ov007_020b2308(file,0)==descriptor);
    set<std::uint32_t>(0,5);set<std::uint16_t>(0xc,64);set<std::uint16_t>(0xe,64);
    set<std::int32_t>(0x14,40*4096);set<std::int32_t>(0x18,20*4096);
    set<std::int32_t>(0x20,24*4096);set<std::int32_t>(0x24,16*4096);
    set<std::uint16_t>(0x34,0x1234);set<std::uint16_t>(0x36,0x2345);set<std::uint16_t>(0x38,0x3456);
    set<std::int32_t>(0x3c,8192);set<std::int32_t>(0x40,4096);
    set<std::uint16_t>(0x44,0x1234);set<std::uint8_t>(0x4e,35);set<std::int32_t>(0x50,31*4096);
    set<std::uint16_t>(0x58,1);set<const void* const*>(0x5c,descriptor_table);
    set<std::uint16_t>(0x60,1);set<std::uint16_t>(0x62,1);set<std::uint32_t>(0x64,3);
    set<std::int32_t>(0x74,2048);set<std::int32_t>(0x78,4096);set<std::uint8_t>(0x7c,25);set<std::uint8_t>(0x7d,36);
    set<std::int16_t>(0x7e,16);
    __wrap_func_ov007_020c421c(object);
    REQUIRE(draws==2&&!faults&&original==1&&rotation[0]==0x1234&&rotation[1]==0x2345&&rotation[2]==0x3456);
    REQUIRE(hooks::menu_sprite_statistics().capture_ticks==1&&ticks==2);
    REQUIRE(saved[0].file_id==222&&saved[0].image==0&&saved[0].polygon_id==35&&saved[1].polygon_id==36);
    REQUIRE(saved[0].geometry.alpha==31&&saved[1].geometry.alpha==6&&saved[1].geometry.colour[0]==0);
    REQUIRE(saved[0].geometry.direct_quad&&!saved[1].geometry.direct_quad&&!saved[0].material.sample_alpha);
    REQUIRE(saved[0].material.wrap_s==Wrap::repeat&&saved[0].material.wrap_t==Wrap::repeat);
    REQUIRE(saved[0].object.value[0]==48*4096&&saved[0].object.value[4]==16*4096);
    REQUIRE(saved[0].object.value[9]==16*4096&&saved[0].object.value[10]==12*4096);
    REQUIRE(saved[1].object.value[0]==24*4096&&saved[1].object.value[9]==28*4096+16);
    draws=0;set<std::uint32_t>(0x6c,1);__wrap_func_ov007_020c421c(object);
    REQUIRE(draws==2&&!faults&&saved[0].material.sample_alpha&&saved[1].material.sample_alpha);
    draws=0;set<std::int32_t>(0x50,0);const auto rotations=rotation_calls;
    __wrap_func_ov007_020c421c(object);REQUIRE(!draws&&!faults&&rotation_calls==rotations);
    set<std::int32_t>(0x50,31*4096);hooks::model_retiring(file);draws=0;
    REQUIRE(!hooks::binding_for_file(file));
    __wrap_func_ov007_020c421c(object);REQUIRE(draws==2&&!faults&&saved[0].file_id==222); // BMD may be freed first
    hooks::direct_model_loaded(226,file2);__wrap_func_ov007_020b22e0(descriptor,file2,0);hooks::model_retiring(file2);draws=0;
    __wrap_func_ov007_020c421c(object);REQUIRE(draws==2&&!faults&&refreshes==1&&saved[0].file_id==226);
    REQUIRE(__wrap_func_ov007_020c1620(descriptor)==0x1234&&destroyed==1&&!faults);draws=0;
    __wrap_func_ov007_020c421c(object);REQUIRE(!draws&&!faults&&hooks::statistics().producer_failures==1);
    REQUIRE(hooks::activate(false));__wrap_func_ov007_020c421c(object);REQUIRE(original==2&&!faults);
    hooks::menu_sprite_clock(nullptr);REQUIRE(ticks==hooks::menu_sprite_statistics().capture_ticks*2);
    return 0;
}
}
