#include "lighting_palette.hpp"
#include <array>
#include <cassert>
#include <cstring>
using namespace sm64ds::render;
int main(){
    std::array<LightSet,2> lights{};std::array<PreparedLighting,4> states{};
    LightingPalette palette(lights.data(),lights.size(),states.data(),states.size());
    LightSet source;source.known=15;source.table_known=true;source.light[2]={{1,2,3},{4,5,6},512};
    LightingInput input{&source,{4096,0,0,0,4096,0,0,0,4096},{1,2,3,4,4,true}};
    std::uint16_t id=99;assert(palette.intern(input,id)==Error::none&&id==0);
    assert(palette.view().light_count==1&&palette.view().state_count==1);
    for(unsigned i=0;i<2000;++i){++source.generation;source.light[0].direction[0]=std::int16_t(i%1024);
        assert(palette.intern(input,id)==Error::none&&id==0);}
    auto first=palette.view();LightingInput resolved;
    assert(resolve_lighting(first,0,resolved)==Error::none&&resolved.lights!=&source&&resolved.lights->known==4);
    source.light[2].colour[0]=7;source.shininess[0]=250;input.normal[0]=8192;
    assert(palette.intern(input,id)==Error::none&&id==1);
    assert(first.state[0].normal[0]==4096&&first.lights[0].light[2].colour[0]==4&&first.lights[0].shininess[0]==0);
    const auto mark=palette.mark();input.normal[0]=12288;
    assert(palette.intern(input,id)==Error::none&&id==2&&palette.view().light_count==2);
    palette.rollback(mark);assert(palette.view().state_count==2&&palette.view().light_count==2);
    assert(palette.intern(input,id)==Error::none&&id==2);
    source.light[2].colour[0]=8;id=999;
    assert(palette.intern(input,id)==Error::capacity&&id==999&&palette.view().state_count==3&&palette.view().light_count==2);
    source.light[2].colour[0]=7;input.material.mask=0;input.lights=nullptr;
    assert(palette.intern(input,id)==Error::none&&id==3);
    for(unsigned i=0;i<100;++i){input.normal[0]=std::int32_t(i);
        assert(palette.intern(input,id)==Error::none&&id==3);}
    assert(resolve_lighting(palette.view(),3,resolved)==Error::none&&!resolved.lights);
    input.material.emission=9;id=999;
    assert(palette.intern(input,id)==Error::capacity&&id==999&&palette.view().state_count==4);
    input.material.mask=16;assert(palette.intern(input,id)==Error::invalid_data&&id==999);
    palette.reset();assert(!palette.view().state_count&&!palette.view().light_count);
    input.material.mask=4;input.lights=&source;
    assert(palette.intern(input,id)==Error::none&&id==0);
    source.known=0;id=999;
    assert(palette.intern(input,id)==Error::unsupported&&id==999&&palette.view().state_count==1);
    assert(resolve_lighting(palette.view(),1,resolved)==Error::invalid_data);
    // Fill every permitted state. Hash collisions must neither merge unequal
    // bases nor lose an earlier value; a capacity failure cannot add a light.
    static std::array<PreparedLighting,LightingPalette::max_states> large;
    LightingPalette full(nullptr,0,large.data(),large.size());input.material.mask=0;input.lights=nullptr;
    for(unsigned i=0;i<large.size();++i){input.material.emission=std::uint16_t(i);
        assert(full.intern(input,id)==Error::none&&id==i);}
    for(unsigned i=0;i<large.size();++i){input.material.emission=std::uint16_t(i);
        assert(full.intern(input,id)==Error::none&&id==i);}
    input.material.emission=2048;id=999;
    assert(full.intern(input,id)==Error::capacity&&id==999);

    // Different masks and lookup modes retain independent hints. Changing a
    // source in place without updating generation must still capture its value.
    std::array<LightSet,16> modes_lights{};std::array<PreparedLighting,32> modes_states{};
    LightingPalette modes(modes_lights.data(),modes_lights.size(),modes_states.data(),modes_states.size());
    source={};source.known=15;source.table_known=source.toon_known=true;
    input={&source,{4096,0,0,0,4096,0,0,0,4096},{1,2,3,4,1,true}};
    assert(modes.intern(input,id)==Error::none&&id==0);
    input.material.mask=2;assert(modes.intern(input,id)==Error::none&&id==1);
    input.material.mask=1;assert(modes.intern(input,id)==Error::none&&id==0);
    source.shininess[127]=9;assert(modes.intern(input,id)==Error::none&&id==2);
    input.material.use_shininess=false;input.material.toon=true;
    assert(modes.intern(input,id)==Error::none&&id==3);
    source.toon[31]=0x1234;assert(modes.intern(input,id)==Error::none&&id==4);
    const auto checkpoint=modes.mark();source.toon[31]=0x2345;
    assert(modes.intern(input,id)==Error::none&&id==5);
    modes.rollback(checkpoint);source.toon[31]=0x3456;
    assert(modes.intern(input,id)==Error::none&&id==5);
    assert(resolve_lighting(modes.view(),id,resolved)==Error::none&&resolved.lights->toon[31]==0x3456);
    modes.reset();source.toon[31]=0x4567;
    assert(modes.intern(input,id)==Error::none&&id==0);
    assert(resolve_lighting(modes.view(),id,resolved)==Error::none&&resolved.lights->toon[31]==0x4567);
}
