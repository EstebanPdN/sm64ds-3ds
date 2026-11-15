#include "fixed_uv.hpp"
#include <cassert>
#include <iomanip>
#include <iostream>
using namespace sm64ds::render;
int main() {
    UvDomain domain;domain.include(-256.f,-256.f);assert(domain.accepts(8,8));
    domain.include(256.f,0.f);assert(!domain.accepts(8,8)); // raw32768 is outside signed16
    domain={};domain.include(1.f/16384,1.f);assert(!domain.accepts(8,8)&&domain.accepts(1024,1024));
    domain={};domain.include(.1f,.1f);assert(!domain.accepts(1024,1024));
    FixedUv invalid;invalid.width=9;FixedUvUniforms retained;retained.row[0][0]=123;
    assert(pack_fixed_uv(invalid,16,16,retained)==Error::invalid_data && retained.row[0][0]==123);
    Material m,same;m.uv_mode=UvMode::fixed;assert(valid(m));same=m;
    assert(m==same);same.fixed_uv.coefficient[0]++;assert(!(m==same));
    m.fixed_uv.height=3;assert(!valid(m));m.uv_mode=static_cast<UvMode>(99);assert(!valid(m));
    std::cout<<std::setprecision(9);
    FixedUv input;unsigned width,height,sampled_width,sampled_height;
    while(std::cin>>input.coefficient[0]){
        for(unsigned i=1;i<6;++i)std::cin>>input.coefficient[i];
        std::cin>>width>>height>>sampled_width>>sampled_height;assert(std::cin.good());
        input.width=width;input.height=height;FixedUvUniforms output;
        assert(pack_fixed_uv(input,sampled_width,sampled_height,output)==Error::none);
        for(const auto& row:output.row)for(float value:row)std::cout<<value<<' ';
        std::cout<<'\n';
    }
}
