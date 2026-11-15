#include "game_material.hpp"
#include <cassert>
#include <iostream>
using namespace sm64ds::render;
using namespace sm64ds::render::game;
int main(){
    RuntimeMaterial source{};source.scale_x=source.scale_y=4096;source.polygon=(31u<<16)|192;
    source.diffuse_ambient=0x801f;source.specular_emission=992u<<16;
    SelectedImage image;image.texture=4;image.palette=7;image.sampled={16,32};LiveMaterial result;
    assert(capture_material(source,image,{32,16},{32,16},0,4096,result)==Error::none);
    assert(result.texture==4&&result.palette==7&&result.diffuse==0xff0000ff&&result.emission==0x00ff00ff);
    assert(result.state.uv[0]==2 && result.state.uv[4]==.5 && result.state.uv[5]==0);
    source.texture_flags=1u<<30;
    assert(capture_material(source,image,{32,16},{32,16},0,4096,result)==Error::none);
    assert(result.state.uv_mode==UvMode::fixed&&result.state.fixed_uv.width==32&&result.state.fixed_uv.height==16);
    const auto retained=result.state.fixed_uv;
    source.polygon|=1u<<15;
    assert(capture_material(source,image,{32,16},{32,16},0,4096,result)==Error::unsupported&&result.state.fixed_uv==retained);
    source.polygon=0x80000000u;image.texture=999999;
    assert(capture_material(source,image,{0,0},{0,0},0,0,result)==Error::none&&!result.visible);
    // Text protocol for independent wide-integer source equation checks.
    source.polygon=(31u<<16)|192;source.texture_flags=1u<<30;image.texture=0;image.sampled={16,16};
    unsigned w,h,bw,bh;int sine,cosine;
    while(std::cin>>source.scale_x>>source.scale_y>>source.translate_x>>source.translate_y>>w>>h>>bw>>bh>>sine>>cosine){
        assert(capture_material(source,image,{bw,bh},{w,h},sine,cosine,result)==Error::none);
        for(auto value:result.state.fixed_uv.coefficient)std::cout<<value<<' ';
        std::cout<<'\n';
    }
}
