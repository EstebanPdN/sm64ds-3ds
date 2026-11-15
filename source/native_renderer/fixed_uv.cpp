#include "fixed_uv.hpp"

namespace sm64ds::render {
Error pack_fixed_uv(const FixedUv& input,unsigned width,unsigned height,FixedUvUniforms& output) noexcept {
    if(!texture_dimensions(input.width,input.height)||!texture_dimensions(width,height))return Error::invalid_data;
    FixedUvUniforms result;
    constexpr unsigned component[4]={0,1,3,4};
    for(unsigned i=0;i<4;++i){
        const auto value=std::uint32_t(input.coefficient[component[i]]);
        result.row[0][i]=float(value&255);
        result.row[1][i]=float((value>>8)&15);
        result.row[2][i]=float((value>>12)&255);
        const unsigned high=(value>>20)&255;
        result.row[3][i]=float(high>=128?int(high)-256:int(high));
    }
    for(unsigned i=0;i<2;++i){
        const auto value=std::uint32_t(input.coefficient[i?5:2]);
        result.row[4][i*2]=float(value&4095);
        const unsigned high=(value>>12)&65535;
        result.row[4][i*2+1]=float(high>=32768?int(high)-65536:int(high));
    }
    result.row[5]={{float(input.width)*16,float(input.height)*16,0,0}};
    result.row[6]={{1.f/(float(width)*16),1.f/(float(height)*16),0,0}};
    output=result;return Error::none;
}
}
