#include "normal_uv.hpp"
#include <cassert>
#include <iomanip>
#include <iostream>
using namespace sm64ds::render;
int main(){
    NormalUv input;NormalUvUniforms output;unsigned w,h,sw,sh;int x,y;
    input.width=3;output.row[0][0]=123;
    assert(pack_normal_uv(input,8,8,output)==Error::invalid_data&&output.row[0][0]==123);
    Material m;m.uv_mode=UvMode::normal;assert(valid(m));auto same=m;
    same.normal_uv().offset[1]=1;assert(!(m==same));same=m;same.normal_uv().coefficient[5]=1;assert(!(m==same));
    std::cout<<std::setprecision(9);
    while(std::cin>>input.coefficient[0]){
        for(unsigned i=1;i<6;++i)std::cin>>input.coefficient[i];
        std::cin>>w>>h>>sw>>sh>>x>>y;assert(std::cin.good());
        input.width=w;input.height=h;input.offset={{std::int16_t(x),std::int16_t(y)}};
        assert(pack_normal_uv(input,sw,sh,output)==Error::none);
        for(const auto& row:output.row)for(float v:row)std::cout<<v<<' ';
        std::cout<<'\n';
    }
}
