#include "game_particle_geometry.hpp"
#include <cstring>

namespace sm64ds::render::game {
namespace {
std::int32_t word(std::uint32_t bits) noexcept {std::int32_t value;std::memcpy(&value,&bits,4);return value;}
std::int32_t rounded(std::int32_t a,std::int32_t b) noexcept {
    return word(std::uint32_t(std::uint64_t(std::int64_t(a)*b+2048)>>12));
}
float endpoint(unsigned exponent,unsigned sample,unsigned image) noexcept {
    const auto bits=std::uint16_t((16u<<exponent)*sample);
    const int value=bits<32768?int(bits):int(bits)-65536;
    return float(value)/(16.f*image);
}
std::uint8_t channel(unsigned value) noexcept {return std::uint8_t((value<<3)|(value>>2));}
}
Error particle_billboard(const ParticleBillboard& input,ParticleBillboardGeometry& output) noexcept {
    if(input.alpha>31||input.colour>32767||input.repeat_s>3||input.repeat_t>3||
       !texture_dimensions(input.image_width,input.image_height))return Error::invalid_data;
    ParticleBillboardGeometry next;
    const auto height=rounded(input.scale,input.multiplier),width=rounded(height,input.aspect);
    auto& matrix=next.object.value;
    matrix[0]=rounded(input.cosine,width);matrix[1]=rounded(input.sine,width);
    matrix[3]=rounded(-int(input.sine),height);matrix[4]=rounded(input.cosine,height);
    for(unsigned i=0;i<3;++i)matrix[9+i]=input.position[i];
    const auto u=endpoint(input.repeat_s,input.sample_width,input.image_width);
    const auto v=endpoint(input.repeat_t,input.sample_height,input.image_height);
    const auto r=channel(input.colour&31),g=channel((input.colour>>5)&31),b=channel((input.colour>>10)&31);
    const auto a=std::uint8_t(unsigned(input.alpha)*255/31);
    next.vertex={{{-1,1,0,0,0,r,g,b,a},{1,1,0,u,0,r,g,b,a},{1,-1,0,u,v,r,g,b,a},{-1,-1,0,0,v,r,g,b,a}}};
    output=next;return Error::none;
}
}
