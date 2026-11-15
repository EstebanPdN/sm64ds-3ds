#include "game_sprite_geometry.hpp"
namespace sm64ds::render::game {
namespace {
int signed_uv(std::int64_t value) noexcept {
    const auto bits=std::uint16_t(std::uint64_t(value)&65535u);
    return bits>=32768?int(bits)-65536:int(bits);
}
std::uint8_t channel(unsigned value) noexcept {return std::uint8_t((value<<3)|(value>>2));}
void quad(const SpriteGeometry& s,Vertex* v,float x0,float y0,float x1,float y1,
          std::int64_t u0,std::int64_t t0,std::int64_t u1,std::int64_t t1,bool corners) noexcept {
    const float xs[]={x0,x0,x1,x1},ys[]={y1,y0,y0,y1};
    const auto a=std::uint8_t(unsigned(s.alpha)*255/31);
    const float us[]={float(signed_uv(u0)),float(signed_uv(u0)),float(signed_uv(u1)),float(signed_uv(u1))};
    const float ts[]={float(signed_uv(t0)),float(signed_uv(t1)),float(signed_uv(t1)),float(signed_uv(t0))};
    for(unsigned i=0;i<4;++i){const auto c=s.colour[corners?i:0];
        v[i]={xs[i],ys[i],0,us[i]/(16.f*s.image_width),ts[i]/(16.f*s.image_height),
            channel(c&31),channel((c>>5)&31),channel((c>>10)&31),a};}
}
}
Error sprite_geometry(const SpriteGeometry& s,Vertex* output,std::size_t capacity,SpriteGeometryResult& result) noexcept {
    if(!texture_dimensions(s.image_width,s.image_height)||s.sample_width>1024||s.sample_height>1024||
       s.repeat_s>65535||s.repeat_t>65535||s.alpha>31||(!s.tiles&&s.tile_count))return Error::invalid_data;
    for(const auto c:s.colour)if(c>32767)return Error::invalid_data;
    if(!s.alpha){result={};return Error::none;}
    if(s.direct_quad){
        if(!output||capacity<4)return Error::capacity;
        quad(s,output,0,0,1,1,std::int64_t(s.offset_s)*16,std::int64_t(s.offset_t)*16,
            (std::int64_t(s.offset_s)+std::int64_t(s.sample_width)*s.repeat_s)*16,
            (std::int64_t(s.offset_t)+std::int64_t(s.sample_height)*s.repeat_t)*16,s.corner_colours);
        result={4,1,false};return Error::none;
    }
    const auto cols=s.sample_width/8,rows=s.sample_height/8;
    if(!cols||!rows)return Error::unsupported; // original division is undefined; do not invent geometry
    const auto total=std::size_t(cols)*rows;
    if(s.tiles&&s.tile_count<total)return Error::invalid_data;
    const auto u0=std::int64_t(s.offset_s)*16,t0=std::int64_t(s.offset_t)*16;
    const auto u1=u0+std::int64_t(s.repeat_s)*128*cols,t1=t0+std::int64_t(s.repeat_t)*128*rows;
    const auto continuous=[](std::int64_t a,std::int64_t b){return a>=-32768&&a<=32767&&b>=-32768&&b<=32767;};
    const bool collapsed=!s.tiles&&continuous(u0,u1)&&continuous(t0,t1);
    std::size_t visible=total;
    if(s.tiles){
        visible=0;
        const auto available=std::size_t(s.image_width/8)*(s.image_height/8);
        for(std::size_t i=0;i<total;++i){const auto tile=s.tiles[i];if(tile==65535)continue;
            if(tile>=available)return Error::invalid_data;
            ++visible;
        }
    }
    const auto count=collapsed?std::size_t(4):visible*4;
    if(count>max_frame_quads*4||count>capacity||(count&&!output))return Error::capacity;
    const int dx=4096/int(cols),dy=4096/int(rows);
    if(collapsed)quad(s,output,0,0,float(dx*cols)/4096.f,float(dy*rows)/4096.f,u0,t0,u1,t1,false);
    else{
        std::size_t at=0;
        for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<cols;++x){
            if(s.tiles&&s.tiles[y*cols+x]==65535)continue;
            std::int64_t left,top,right,bottom;
            if(s.tiles){const auto tile=s.tiles[y*cols+x];
                left=(tile%(s.image_width/8))*128;top=(tile/(s.image_width/8))*128;right=left+128;bottom=top+128;
            }else{
                left=u0+std::int64_t(x)*s.repeat_s*128;top=t0+std::int64_t(y)*s.repeat_t*128;
                right=left+std::int64_t(s.repeat_s)*128;bottom=top+std::int64_t(s.repeat_t)*128;
            }
            quad(s,output+at,float(dx*x)/4096.f,float(dy*(rows-y-1))/4096.f,
                float(dx*(x+1))/4096.f,float(dy*(rows-y))/4096.f,left,top,right,bottom,false);at+=4;
        }
    }
    result={count,visible,collapsed};return Error::none;
}
}
