#include "game_lighting.hpp"
#include <array>
#include <cassert>
#include <cstring>
using namespace sm64ds::render;
namespace hooks=sm64ds::render::game_hooks;
static bool active=false;static unsigned forwarded=0,failures=0;
namespace sm64ds::render::game_hooks {bool native_active() noexcept{return active;}void report_image_failure() noexcept{++failures;}}
extern "C" void __real_func_ov007_020c232c(void*){++forwarded;}
extern "C" void __wrap_func_ov007_020c232c(void*);
int main(){
    __wrap_func_ov007_020c232c(nullptr);assert(forwarded==1&&!failures);active=true;hooks::lighting_reset();
    std::array<unsigned char,40> source{};
    auto word=[&](unsigned at,int value){std::memcpy(source.data()+at,&value,4);};
    auto half=[&](unsigned at,std::int16_t value){std::memcpy(source.data()+at,&value,2);};
    word(36,1);half(28,4095);half(30,-4096);half(32,7);half(34,0x7fff);
    __wrap_func_ov007_020c232c(source.data());assert(failures==1&&!hooks::lighting_known());
    const std::int32_t view[12]={4096,0,0,0,4096,0,0,0,4096,0,0,0};hooks::lighting_view(view);
    __wrap_func_ov007_020c232c(source.data());LightSet values;
    assert(hooks::capture_lighting(1,false,values)==Error::none&&values.light[0].direction[0]==-511&&values.light[0].direction[1]==512);
    assert(values.light[0].colour[0]==31&&values.light[0].colour[1]==31&&values.light[0].colour[2]==31);
    const auto generation=values.generation;
    for(unsigned i=0;i<1000;++i)__wrap_func_ov007_020c232c(source.data());
    assert(hooks::capture_lighting(1,false,values)==Error::none&&values.generation==generation);
    word(36,0);half(28,0);__wrap_func_ov007_020c232c(source.data());
    assert(hooks::capture_lighting(1,false,values)==Error::none&&values.light[0].direction[0]==-511);
    word(36,1);word(0,4);__wrap_func_ov007_020c232c(source.data());assert(failures==2&&!hooks::lighting_known());
    hooks::lighting_reset();assert(hooks::capture_lighting(1,false,values)==Error::unsupported);
    active=false;__wrap_func_ov007_020c232c(source.data());assert(forwarded==2);
}
