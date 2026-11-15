#include "canvas_windows.hpp"
#include <cassert>
#include <cstring>
#include <limits>
using namespace sm64ds::render;
namespace {
int modulo(std::int64_t value,int size){const auto r=value%size;return int(r<0?r+size:r);}
// Independent point-domain oracle. It first wraps a destination coordinate,
// then samples the translated finite source; no window construction is used.
void inspect(const CanvasCrop& crop){
    CanvasWindows windows;assert(canvas_windows(crop,windows)==Error::none&&windows.count<=4);
    for(int y=0;y<crop.viewport_height;++y)for(int x=0;x<crop.viewport_width;++x){
        const auto sx=std::int64_t(modulo(std::int64_t(x)+crop.scroll_x,crop.width))-crop.translate_x;
        const auto sy=std::int64_t(modulo(std::int64_t(y)+crop.scroll_y,crop.height))-crop.translate_y;
        const bool expected=sx>=0&&sx<crop.width&&sy>=0&&sy<crop.height;unsigned covered=0;
        for(unsigned i=0;i<windows.count;++i){const auto& w=windows.window[i];
            assert(w.left<w.right&&w.top<w.bottom&&w.right<=crop.viewport_width&&w.bottom<=crop.viewport_height);
            if(x>=w.left&&x<w.right&&y>=w.top&&y<w.bottom){++covered;assert(x-w.x==sx&&y-w.y==sy);}
        }
        assert(covered==unsigned(expected));
    }
}
}
int main(){
    CanvasCrop crop{32,24,31,23};CanvasWindows out;out.count=4;const auto before=out;
    auto bad=crop;bad.width=0;assert(canvas_windows(bad,out)==Error::invalid_data&&std::memcmp(&out,&before,sizeof(out))==0);
    bad=crop;bad.height=4097;assert(canvas_windows(bad,out)==Error::invalid_data);
    bad=crop;bad.viewport_height=25;assert(canvas_windows(bad,out)==Error::invalid_data);
    unsigned random=0x43524f50;const auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(unsigned i=0;i<4096;++i){
        crop.width=std::uint16_t(1+next()%32);crop.height=std::uint16_t(1+next()%32);
        crop.viewport_width=std::uint16_t(1+next()%crop.width);crop.viewport_height=std::uint16_t(1+next()%crop.height);
        crop.translate_x=int(next()%129)-64;crop.translate_y=int(next()%129)-64;
        crop.scroll_x=static_cast<std::int32_t>(next());crop.scroll_y=static_cast<std::int32_t>(next());inspect(crop);
    }
    for(int translation:{-33,-32,-31,-1,0,1,31,32,33,std::numeric_limits<int>::min(),std::numeric_limits<int>::max()})
        for(int scroll:{-33,-32,-31,-1,0,1,31,32,33,std::numeric_limits<int>::min(),std::numeric_limits<int>::max()}){
            crop={32,32,32,32,translation,-(translation/2),scroll,scroll};inspect(crop);
        }
    crop={256,256,256,192,0,-192,0,0};assert(canvas_windows(crop,out)==Error::none&&out.count==1);
    assert(out.window[0].top==0&&out.window[0].bottom==64&&out.window[0].y==-192);
    CanvasPlacement physical;assert(canvas_placement(crop,out.window[0],Screen::top,{40,0,320,240},physical)==Error::none);
    assert(physical.rectangle.x==40-1.f/16&&physical.rectangle.y==-240-1.f/16&&physical.rectangle.height==320);
    assert(physical.clip.left==40&&physical.clip.right==360&&physical.clip.top==0&&physical.clip.bottom==80);
    const auto keep=physical;assert(canvas_placement(crop,out.window[0],Screen::bottom,{40,0,320,240},physical)==Error::invalid_data);
    assert(physical.rectangle.x==keep.rectangle.x&&physical.clip==keep.clip);
    crop={4096,4096,1,1};CanvasWindow oversized{0,0,0,0,1,1};
    assert(canvas_placement(crop,oversized,Screen::top,{0,0,400,240},physical)==Error::invalid_data);
    assert(physical.rectangle.x==keep.rectangle.x&&physical.clip==keep.clip);
}
