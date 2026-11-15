#include "lighting_uniforms.hpp"
#include <cassert>
#include <iomanip>
#include <iostream>
using namespace sm64ds::render;
void check_cache(){
    LightSet lights;lights.known=15;lights.table_known=true;
    lights.light[2]={{1,2,3},{4,5,6},512};
    LightingInput input{&lights,{4096,0,0,0,4096,0,0,0,4096},{31,0,1,0,4,true}};
    LightingCache cache;LightingChanges change;
    assert(cache.prepare(input,change)==Error::none&&change.packed&&change.normal&&change.shininess);
    assert(cache.uniforms().count==1&&cache.uniforms().direction[0][0]==1);
    for(unsigned i=0;i<1000;++i){
        ++lights.generation;lights.light[0].colour[0]=i%32; // irrelevant disabled source
        assert(cache.prepare(input,change)==Error::none&&!change.packed&&!change.normal&&!change.shininess);
    }
    lights.light[2].colour[0]=7; // no generation bump: addresses/generations cannot be identities
    assert(cache.prepare(input,change)==Error::none&&change.packed&&change.colour&&!change.normal&&!change.direction);
    lights.shininess[0]=254;
    assert(cache.prepare(input,change)==Error::none&&change.shininess&&!change.colour&&!change.material);
    const auto saved=cache.uniforms().direction;
    lights.light[2].direction[0]=1024;change.packed=false;
    assert(cache.prepare(input,change)==Error::invalid_data&&!change.packed&&cache.uniforms().direction==saved);
    lights.light[2].direction[0]=1;
    assert(cache.prepare(input,change)==Error::none&&!change.packed);
    cache.invalidate();
    assert(cache.prepare(input,change)==Error::none&&!change.packed&&change.normal&&change.direction&&change.shininess&&change.switches);
    input.material.mask=0;input.normal[0]=0;input.lights=nullptr;
    assert(cache.prepare(input,change)==Error::none&&!change.normal&&!change.shininess&&cache.uniforms().count==0);
    input.material.mask=4;input.lights=&lights;
    assert(cache.prepare(input,change)==Error::none&&change.normal&&change.direction&&change.colour&&change.shininess);
    input.material.use_shininess=false;
    assert(cache.prepare(input,change)==Error::none&&!change.shininess&&change.switches);
    lights.shininess.fill(0);input.material.use_shininess=true;
    assert(cache.prepare(input,change)==Error::none&&change.shininess&&change.switches);
    std::uint32_t seed=0x534d3634;
    for(unsigned pose=0;pose<1000;++pose){
        for(auto& component:input.normal){seed=seed*1664525u+1013904223u;std::memcpy(&component,&seed,4);}
        if(pose%31==0)cache.invalidate();
        assert(cache.prepare(input,change)==Error::none&&change.packed);
        LightingUniforms reference;assert(pack_lighting(input,reference)==Error::none);
        const auto& retained=cache.uniforms();
        assert(retained.normal==reference.normal&&retained.direction==reference.direction&&retained.colour==reference.colour&&
               retained.material==reference.material&&retained.shininess==reference.shininess&&retained.count==reference.count);
        if(pose%31!=0)assert(!change.direction&&!change.colour&&!change.material&&!change.shininess&&!change.switches&&!change.count);
    }
    lights.known=0;
    assert(cache.prepare(input,change)==Error::unsupported);
    lights.known=4;input.material.use_shininess=false;input.material.toon=true;
    assert(cache.prepare(input,change)==Error::unsupported);
    lights.toon_known=true;lights.toon[3]=0x421f;
    assert(cache.prepare(input,change)==Error::none&&change.switches&&change.shininess&&cache.uniforms().toon);
    assert(cache.uniforms().shininess[3][0]==31&&cache.uniforms().shininess[3][1]==16&&cache.uniforms().shininess[3][2]==16);
    lights.toon[3]=31;
    assert(cache.prepare(input,change)==Error::none&&change.shininess&&!change.normal);
    input.material.toon=false;input.material.use_shininess=true;
    assert(cache.prepare(input,change)==Error::none&&change.switches&&change.shininess&&!cache.uniforms().toon);
}
int main(){
    check_cache();
    LightingInput input;LightSet lights;input.lights=&lights;LightingUniforms packed;packed.count=99;
    input.material.mask=1;assert(pack_lighting(input,packed)==Error::unsupported&&packed.count==99);
    lights.known=15;input.material.specular=1;input.material.use_shininess=true;
    assert(pack_lighting(input,packed)==Error::unsupported&&packed.count==99);
    input.material.specular=0;assert(pack_lighting(input,packed)==Error::none&&!packed.table&&!packed.specular);
    input.material.mask=16;assert(pack_lighting(input,packed)==Error::invalid_data);
    std::cout<<std::setprecision(9);
    while(std::cin>>input.normal[0]){
        for(unsigned i=1;i<9;++i)std::cin>>input.normal[i];
        unsigned diffuse,ambient,specular,emission,mask,table,known;
        std::cin>>diffuse>>ambient>>specular>>emission>>mask>>table>>known;
        input.material={std::uint16_t(diffuse),std::uint16_t(ambient),std::uint16_t(specular),std::uint16_t(emission),std::uint8_t(mask),bool(table)};
        lights.table_known=bool(known);lights.known=15;
        for(auto& light:lights.light){
            for(auto& v:light.direction)std::cin>>v;
            for(auto& v:light.colour){unsigned value;std::cin>>value;v=value;}
            std::cin>>light.specular_reciprocal;
        }
        for(auto& v:lights.shininess){unsigned value;std::cin>>value;v=value;}
        assert(std::cin.good()&&pack_lighting(input,packed)==Error::none);
        std::cout<<packed.count<<' '<<packed.specular<<' '<<packed.table<<' ';
        const auto print=[](const auto& rows){for(const auto& row:rows)for(float v:row)std::cout<<v<<' ';};
        print(packed.normal);print(packed.direction);print(packed.colour);print(packed.material);print(packed.shininess);std::cout<<'\n';
    }
}
