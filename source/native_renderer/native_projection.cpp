#include "native_projection.hpp"
#include <cmath>
namespace sm64ds::render {
Error native_projection(const std::int32_t* source,float width,Matrix& output) noexcept {
    if(!source||!std::isfinite(width)||width<=0||width>1)return Error::invalid_data;
    Matrix next{};
    for(unsigned c=0;c<4;++c){
        const float x=float(source[c*4])/4096.f,y=float(source[c*4+1])/4096.f;
        const float z=float(source[c*4+2])/4096.f,w=float(source[c*4+3])/4096.f;
        next.row[0][c]=y;next.row[1][c]=-width*x;
        next.row[2][c]=(z-w)*.5f;next.row[3][c]=w;
    }
    output=next;return Error::none;
}
}
