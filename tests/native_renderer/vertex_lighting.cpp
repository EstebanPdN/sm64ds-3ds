#include "vertex_lighting.hpp"
#include <iostream>
using namespace sm64ds::render;
int main(){
    for(;;){
        LightingInput input;LightSet lights;input.lights=&lights;lights.known=15;
        if(!(std::cin>>input.normal[0]))break;
        for(unsigned i=1;i<9;++i)std::cin>>input.normal[i];
        unsigned mask,table,toon,grey;std::cin>>input.material.diffuse>>input.material.ambient
            >>input.material.specular>>input.material.emission>>mask>>table>>toon>>grey;
        if(mask>15||table>1||toon>1||grey>1)return 2;
        input.material.mask=std::uint8_t(mask);input.material.use_shininess=table;
        input.material.toon=toon;input.material.grey=grey;lights.table_known=true;lights.toon_known=true;
        for(auto& light:lights.light){unsigned r,g,b;
            std::cin>>light.direction[0]>>light.direction[1]>>light.direction[2]>>r>>g>>b>>light.specular_reciprocal;
            if(r>31||g>31||b>31)return 2;
            light.colour={std::uint8_t(r),std::uint8_t(g),std::uint8_t(b)};
        }
        for(auto& value:lights.shininess){unsigned n;std::cin>>n;if(n>255)return 2;value=std::uint8_t(n);}
        for(auto& value:lights.toon)std::cin>>value;
        std::array<std::int16_t,3> normal;std::cin>>normal[0]>>normal[1]>>normal[2];
        if(!std::cin||validate_lighting(input)!=Error::none)return 2;
        for(auto n:normal)if(n<-512||n>511)return 2;
        const auto colour=vertex_lighting(input,normal);
        std::cout<<unsigned(colour[0])<<' '<<unsigned(colour[1])<<' '<<unsigned(colour[2])<<' '<<unsigned(colour[3])<<'\n';
    }
}
