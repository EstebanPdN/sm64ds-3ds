#include "vertex_lighting.hpp"
#include <algorithm>
#include <cstring>
namespace sm64ds::render {
namespace {
static_assert((std::int32_t(-1)>>1)==-1,"Lighting requires arithmetic signed shifts on the detected compiler");
template<unsigned Bits> int signed_bits(std::int32_t value) noexcept {
    const auto low=std::uint32_t(value)&((1u<<Bits)-1);
    return low&(1u<<(Bits-1))?int(low)-int(1u<<Bits):int(low);
}
}
PreparedVertexLighting::PreparedVertexLighting(const LightingInput& input) noexcept:
    normal_(input.normal),specular_(input.material.specular!=0),
    table_(input.material.specular&&input.material.use_shininess),toon_mode_(input.material.toon),grey_(input.material.grey){
    const auto& m=input.material;
    for(unsigned c=0;c<3;++c)base_[c]=std::uint32_t((m.emission>>(c*5))&31)<<14;
    for(unsigned slot=0;slot<4;++slot){
        if(!(m.mask&(1u<<slot)))continue;
        const auto& source=input.lights->light[slot];auto& light=light_[count_++];
        light.direction=source.direction;light.reciprocal=std::max<std::int32_t>(0,source.specular_reciprocal);
        for(unsigned c=0;c<3;++c){
            light.diffuse[c]=((m.diffuse>>(c*5))&31)*source.colour[c];
            light.specular[c]=((m.specular>>(c*5))&31)*source.colour[c];
            base_[c]+=(((m.ambient>>(c*5))&31)*source.colour[c])<<9;
        }
    }
    if(count_&&table_)shininess_=input.lights->shininess;
    if(toon_mode_)toon_=input.lights->toon;
}
std::array<std::uint8_t,4> PreparedVertexLighting::colour(const std::array<std::int16_t,3>& source) const noexcept {
    auto acc=base_;
    if(count_){
        int normal[3];
        for(unsigned row=0;row<3;++row){
            // Only bits12..22 survive >>12/signed11. Low32-bit products
            // preserve these bits even when the wide mathematical sum wraps.
            std::uint32_t sum=0;
            for(unsigned axis=0;axis<3;++axis)sum+=std::uint32_t(source[axis])*std::uint32_t(normal_[axis*3+row]);
            const auto bits=sum<<9;std::int32_t signed_word;std::memcpy(&signed_word,&bits,4);
            normal[row]=signed_word>>21;
        }
        for(unsigned slot=0;slot<count_;++slot){const auto& light=light_[slot];
            int dot=0;for(unsigned axis=0;axis<3;++axis)dot+=(int(light.direction[axis])*normal[axis])>>9;
            int shine=0;
            if(dot>0){
                const auto diffuse=signed_bits<11>(dot);
                for(unsigned c=0;c<3;++c)acc[c]+=(light.diffuse[c]*std::uint32_t(diffuse))&0xfffffu;
                if(specular_){
                    const auto half=signed_bits<11>(dot+normal[2]);const auto square=(half*half>>10)&1023;
                    // square<=1023 and reciprocal<=262144: signed32 product.
                    const auto value=(square*light.reciprocal>>8)-512;
                    if(value>=0)shine=std::clamp(signed_bits<14>(value),0,511);
                }
            }
            if(table_)shine=int(shininess_[unsigned(shine)>>2])*2;
            for(unsigned c=0;c<3;++c)acc[c]+=light.specular[c]*unsigned(shine);
        }
    }
    std::array<unsigned,3> rgb{};
    for(unsigned c=0;c<3;++c)rgb[c]=unsigned(std::min<std::uint32_t>(31,acc[c]>>14));
    if(toon_mode_){const auto colour=toon_[rgb[0]];for(unsigned c=0;c<3;++c)rgb[c]=(colour>>(c*5))&31;}
    else if(grey_)rgb[1]=rgb[2]=rgb[0];
    return {std::uint8_t((rgb[0]<<3)|(rgb[0]>>2)),std::uint8_t((rgb[1]<<3)|(rgb[1]>>2)),std::uint8_t((rgb[2]<<3)|(rgb[2]>>2)),255};
}
std::array<std::uint8_t,4> vertex_lighting(const LightingInput& input,const std::array<std::int16_t,3>& normal) noexcept {
    return PreparedVertexLighting(input).colour(normal);
}
}
