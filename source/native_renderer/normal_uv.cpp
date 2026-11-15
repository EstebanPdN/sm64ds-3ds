#include "normal_uv.hpp"
namespace sm64ds::render {
Error pack_normal_uv(const NormalUv& input,unsigned width,unsigned height,NormalUvUniforms& output) noexcept {
    if(!texture_dimensions(input.width,input.height)||!texture_dimensions(width,height))return Error::invalid_data;
    NormalUvUniforms next;
    for(unsigned digit=0;digit<7;++digit)for(unsigned row=0;row<2;++row)for(unsigned axis=0;axis<3;++axis){
        const auto word=std::uint32_t(input.coefficient[row*3+axis]);
        const unsigned part=(word>>(digit*5))&(digit==6?3u:31u);
        next.row[digit*2+row][axis]=float(digit==6&&part>=2?int(part)-4:int(part));
    }
    next.row[14]={{float(input.width)*16,float(input.height)*16,1.f/(width*16),1.f/(height*16)}};
    next.row[15]={{float(input.offset[0]),float(input.offset[1]),0,0}};
    output=next;return Error::none;
}
}
