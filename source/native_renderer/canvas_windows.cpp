#include "canvas_windows.hpp"
#include <algorithm>
#include <cmath>

namespace sm64ds::render {
namespace {
struct Segment {std::int32_t origin=0;std::uint16_t first=0,last=0;};
unsigned segments(unsigned size,unsigned viewport,std::int32_t translation,
                  std::int32_t scroll,std::array<Segment,2>& out) noexcept {
    // Reject invisible translations before arithmetic. This also bounds every
    // later origin independently of arbitrarily large incoming scroll values.
    if(std::int64_t(translation)<=-std::int64_t(size)||std::int64_t(translation)>=size)return 0;
    std::int32_t normalized;
    if(!(size&(size-1)))normalized=std::int32_t(std::uint32_t(scroll)&(size-1));
    else {const auto remainder=scroll%std::int32_t(size);normalized=remainder<0?remainder+std::int32_t(size):remainder;}
    unsigned count=0;
    for(unsigned repeat=0;repeat<2;++repeat){
        const auto domain=std::int32_t(repeat*size)-normalized;
        const auto origin=domain+translation;
        const auto first=std::max({std::int32_t(0),domain,origin});
        const auto last=std::min({std::int32_t(viewport),domain+std::int32_t(size),origin+std::int32_t(size)});
        if(first<last)out[count++]={origin,std::uint16_t(first),std::uint16_t(last)};
    }
    return count;
}
}
Error canvas_windows(const CanvasCrop& crop,CanvasWindows& out) noexcept {
    if(!crop.width||!crop.height||crop.width>4096||crop.height>4096||
       !crop.viewport_width||!crop.viewport_height||crop.viewport_width>crop.width||crop.viewport_height>crop.height)return Error::invalid_data;
    std::array<Segment,2> xs{},ys{};
    const auto nx=segments(crop.width,crop.viewport_width,crop.translate_x,crop.scroll_x,xs);
    const auto ny=segments(crop.height,crop.viewport_height,crop.translate_y,crop.scroll_y,ys);
    CanvasWindows next;
    for(unsigned y=0;y<ny;++y)for(unsigned x=0;x<nx;++x){const auto& sx=xs[x];const auto& sy=ys[y];
        next.window[next.count++]={sx.origin,sy.origin,sx.first,sy.first,sx.last,sy.last};
    }
    out=next;return Error::none;
}
Error canvas_placement(const CanvasCrop& crop,const CanvasWindow& w,Screen side,
                       const ScreenRect& viewport,CanvasPlacement& output) noexcept {
    const auto width=screen_width(side);
    if(!width||!crop.width||!crop.height||crop.width>4096||crop.height>4096||
       !crop.viewport_width||!crop.viewport_height||crop.viewport_width>crop.width||crop.viewport_height>crop.height||
       w.left>=w.right||w.top>=w.bottom||w.right>crop.viewport_width||w.bottom>crop.viewport_height||
       w.x< -4096||w.x>8192||w.y< -4096||w.y>8192||
       w.left<w.x||w.top<w.y||std::int32_t(w.right)>w.x+crop.width||std::int32_t(w.bottom)>w.y+crop.height||
       !std::isfinite(viewport.x)||!std::isfinite(viewport.y)||!std::isfinite(viewport.width)||!std::isfinite(viewport.height)||
       viewport.x<0||viewport.y<0||viewport.width<crop.viewport_width||viewport.height<crop.viewport_height||
       viewport.x+viewport.width>width||viewport.y+viewport.height>240)return Error::invalid_data;
    const auto sx=viewport.width/crop.viewport_width,sy=viewport.height/crop.viewport_height;
    const auto edge=[](float value){return std::uint16_t(std::ceil(value-.5f));};
    CanvasPlacement next;
    next.rectangle={viewport.x+w.x*sx-1.f/16,viewport.y+w.y*sy-1.f/16,crop.width*sx,crop.height*sy};
    // A near-zero viewport origin can round toward negative zero, which is
    // safely converted to zero. All upper bounds are validated above.
    next.clip={edge(viewport.x+w.left*sx),edge(viewport.y+w.top*sy),edge(viewport.x+w.right*sx),edge(viewport.y+w.bottom*sy)};
    // Retain the compositor's rectangle contract even for tiny viewports
    // sampling a very large canvas at a large magnification.
    if(!valid(next.clip,width)||std::fabs(next.rectangle.x)>65536||std::fabs(next.rectangle.y)>65536||
       next.rectangle.width>65536||next.rectangle.height>65536)return Error::invalid_data;
    output=next;return Error::none;
}
}
