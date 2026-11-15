#include "lighting.hpp"
#include <cstring>
#include <limits>
namespace sm64ds::render {
namespace {
std::int16_t signed11(std::int64_t value) noexcept {
    const auto low=std::uint64_t(value)&2047u;
    return std::int16_t(low>=1024?int(low)-2048:int(low));
}
std::int64_t shift12(std::int64_t value) noexcept {
    return value>=0?value/4096:-((-value+4095)/4096);
}
}
void LightingState::reset() noexcept {view_={};state_={};view_valid_=false;}
void LightingState::view(const Basis& value) noexcept {view_=value;view_valid_=true;}
bool LightingState::advance() noexcept {
    if(state_.generation==std::numeric_limits<std::uint64_t>::max())return false;
    ++state_.generation;return true;
}
Error LightingState::light(unsigned index,const std::array<std::int16_t,3>& direction,std::uint16_t colour) noexcept {
    if(index>=4)return Error::invalid_data;
    for(auto v:direction)if(v<-512||v>511)return Error::invalid_data;
    if(!view_valid_)return Error::unsupported;
    NativeLight next;std::int16_t z=0;
    for(unsigned row=0;row<3;++row){
        std::int64_t sum=0;for(unsigned k=0;k<3;++k)sum+=std::int64_t(direction[k])*view_[k*3+row];
        const auto value=shift12(sum);next.direction[row]=signed11(-value);
        if(row==2)z=signed11(value);
        next.colour[row]=std::uint8_t((colour>>(row*5))&31);
    }
    const int denominator=512-z;next.specular_reciprocal=denominator?262144/denominator:0;
    const auto bit=std::uint8_t(1u<<index);
    if((state_.known&bit)&&state_.light[index]==next)return Error::none;
    if(!advance())return Error::capacity;
    state_.light[index]=next;state_.known|=bit;return Error::none;
}
Error LightingState::table(const std::uint8_t* values,std::size_t count) noexcept {
    if(!values||count!=state_.shininess.size())return Error::invalid_data;
    if(state_.table_known&&!std::memcmp(state_.shininess.data(),values,count))return Error::none;
    if(!advance())return Error::capacity;
    std::memcpy(state_.shininess.data(),values,count);state_.table_known=true;return Error::none;
}
namespace {bool lenient_lights=false;}
void LightingState::set_lenient(bool value) noexcept {lenient_lights=value;}
Error LightingState::snapshot(unsigned mask,bool needs_table,LightSet& output) const noexcept {
    if(mask>15)return Error::invalid_data;
    if(lenient_lights&&((state_.known&mask)!=mask||(mask&&needs_table&&!state_.table_known))){
        output=state_;
        for(unsigned i=0;i<4;++i)if(!(output.known&(1u<<i))){
            NativeLight light;light.direction={0,362,362};
            for(auto& c:light.colour)c=std::uint8_t(i?12:31);
            light.specular_reciprocal=262144/(512+362);output.light[i]=light;
        }
        output.known=15;
        if(!output.table_known){for(unsigned i=0;i<output.shininess.size();++i)output.shininess[i]=std::uint8_t(i*2);output.table_known=true;}
        return Error::none;
    }
    if((state_.known&mask)!=mask||(mask&&needs_table&&!state_.table_known))return Error::unsupported;
    output=state_;return Error::none;
}
}
