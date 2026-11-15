#pragma once
#include "native_quads.hpp"
#include <algorithm>

namespace sm64ds::render::game {
using SketchPoint=std::array<std::int32_t,3>;
struct SketchStroke {
    const SketchPoint* points=nullptr;
    std::size_t count=0;
    std::int16_t half=0;
    std::int32_t progress=4096;
    std::uint16_t colour=0;
    std::uint8_t alpha=31;
    int (*angle)(int,int)=nullptr;
    const std::int16_t* sin_cos=nullptr; // original 4096-entry interleaved table
};
// c19cc's semantic polyline, including its wrapped angle bisector and animated
// final segment. Output is perimeter-order quads, rather than a GX strip.
inline Error sketch_geometry(const SketchStroke& s,Vertex* out,std::size_t capacity,std::size_t& written) noexcept {
    if((s.count&&!s.points)||!s.angle||!s.sin_cos||s.half<0||s.alpha>31||s.colour>32767)return Error::invalid_data;
    const auto quads=s.count>1?s.count-1:s.count;
    if(quads>capacity/4)return Error::capacity;
    if(quads&&!out)return Error::invalid_data;
    auto vertex=[&](const SketchPoint& p,float x,float y){
        Vertex v{};v.x=float(p[0])/4096+x;v.y=float(p[1])/4096+y;v.z=float(p[2])/4096;
        v.r=std::uint8_t(((s.colour&31)*255+15)/31);
        v.g=std::uint8_t((((s.colour>>5)&31)*255+15)/31);
        v.b=std::uint8_t((((s.colour>>10)&31)*255+15)/31);
        v.a=std::uint8_t((s.alpha*255+15)/31);return v;
    };
    const float half=float(s.half)/4096;
    if(s.count==1){
        out[0]=vertex(s.points[0],-half,half);out[1]=vertex(s.points[0],-half,-half);
        out[2]=vertex(s.points[0],half,-half);out[3]=vertex(s.points[0],half,half);
    }else{
        int angle=0;Vertex previous[2]{};
        for(std::size_t i=0;i<s.count;++i){
            int before=angle;auto p=s.points[i];
            if(i+1<s.count){
                angle=s.angle(s.points[i+1][1]-p[1],s.points[i+1][0]-p[0]);
                if(!i)before=angle;
            }else if(s.progress<4096){
                const auto t=std::max<std::int32_t>(0,s.progress);
                for(unsigned k=0;k<3;++k){
                    const auto sum=std::int64_t(4096-t)*s.points[i-1][k]+std::int64_t(t)*p[k];
                    p[k]=std::int32_t(sum>=0?sum/4096:-((-sum+4095)/4096));
                }
            }
            const auto delta=std::uint16_t(angle-before);
            const int signed_delta=delta>=32768?int(delta)-65536:int(delta);
            const auto index=std::uint16_t(before+signed_delta/2)>>4;
            const float dx=-float(s.sin_cos[index*2])*half/4096;
            const float dy=float(s.sin_cos[index*2+1])*half/4096;
            const Vertex pair[2]={vertex(p,dx,dy),vertex(p,-dx,-dy)};
            if(i){const auto at=(i-1)*4;out[at]=previous[0];out[at+1]=previous[1];out[at+2]=pair[1];out[at+3]=pair[0];}
            previous[0]=pair[0];previous[1]=pair[1];
        }
    }
    written=quads*4;return Error::none;
}
}
