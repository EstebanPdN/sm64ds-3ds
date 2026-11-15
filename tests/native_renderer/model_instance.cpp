#include "model_instance.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <limits>
using namespace sm64ds::render;
int main() {
    const MaterialBinding materials[]={{0,8,8,ColourSource::diffuse,true},
        {0,8,8,ColourSource::emission,true},{0,8,8,ColourSource::literal,true}};
    const PartBinding parts[]={{0,0,false},{1,1,true},{0,2,false}};
    const ImageBinding images[]={{4,7,0,8,8},{4,8,1,8,8}};
    const ModelBindings bindings{261,{},2,1,images,2,materials,3,parts,3};
    std::array<Matrix,2> ordinary{{identity(),identity()}},billboard=ordinary;
    ordinary[0].row[0][3]=12;billboard[1].row[1][3]=37;
    const int owner=0;
    std::array<Texture,2> handles{{{1,0,&owner},{2,1,&owner}}};
    LiveMaterial live;live.texture=4;live.palette=7;live.diffuse=0xff0000ff;live.emission=0x00ff00ff;
    live.state.tint=0xffffff80;live.state.uv={{2,0,-1,0,2,1}};
    InstanceInputs input{ordinary.data(),billboard.data(),2,&live,1,handles.data(),handles.size()};
    std::array<PartInstance,3> captured{};
    assert(capture_instance(bindings,input,captured.data(),captured.size())==Error::none);
    assert(captured[0].material.tint==0xff000080 && captured[1].material.tint==0x00ff0080 &&
        captured[2].material.tint==0xffffff80);
    assert(captured[0].object_to_clip.row[0][3]==12 && captured[1].object_to_clip.row[1][3]==37);
    assert(captured[2].material.texture==handles[0] && captured[2].material.uv==live.state.uv);
    // Snapshot owns its values; later actor/pose updates do not alter it.
    ordinary[0].row[0][3]=99;live.state.uv[0]=3;live.diffuse=0x123456ff;
    assert(captured[0].object_to_clip.row[0][3]==12 && captured[0].material.uv[0]==2);
    live.palette=8;
    assert(capture_instance(bindings,input,captured.data(),3)==Error::none);
    assert(captured[0].material.texture==handles[1] && captured[0].material.tint==0x12345680);
    std::array<unsigned char,sizeof(captured)> before{};std::memcpy(before.data(),captured.data(),sizeof(captured));
    const auto unchanged=[&](){assert(std::memcmp(before.data(),captured.data(),sizeof(captured))==0);};
    live.palette=999;
    assert(capture_instance(bindings,input,captured.data(),3)==Error::invalid_data);unchanged();
    live.palette=8;billboard[1].row[1][0]=std::numeric_limits<float>::quiet_NaN();
    assert(capture_instance(bindings,input,captured.data(),3)==Error::invalid_data);unchanged();
    billboard[1]=identity();handles[1]={};
    assert(capture_instance(bindings,input,captured.data(),3)==Error::stale_handle);unchanged();
    handles[1]={2,1,&owner};live.diffuse_available=false;
    assert(capture_instance(bindings,input,captured.data(),3)==Error::unsupported);unchanged();
    live.diffuse_available=true;live.emission_available=false;
    assert(capture_instance(bindings,input,captured.data(),3)==Error::unsupported);unchanged();
    live.emission_available=true;live.state.uv_mode=UvMode::fixed;live.texture=ModelBank::no_texture;
    assert(capture_instance(bindings,input,captured.data(),3)==Error::invalid_data);unchanged();
    live.state.uv_mode=UvMode::affine;live.texture=4;
    // Original visibility skips pose, texture and invalid unused material work.
    live.visible=false;live.state.blend=static_cast<Blend>(99);input.ordinary=input.billboard=nullptr;
    assert(capture_instance(bindings,input,captured.data(),3)==Error::none);
    for(const auto& part:captured)assert(!part.visible);
    input.material=nullptr;assert(capture_instance(bindings,input,captured.data(),3)==Error::invalid_data);
}
