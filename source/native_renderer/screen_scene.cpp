#include "screen_scene.hpp"
#include <cmath>

namespace sm64ds::render {
namespace {
bool rectangle(const ScreenRect& r) noexcept {
    constexpr float limit=65536;
    return std::isfinite(r.x)&&std::isfinite(r.y)&&std::isfinite(r.width)&&std::isfinite(r.height)&&
        std::fabs(r.x)<=limit&&std::fabs(r.y)<=limit&&r.width>0&&r.height>0&&r.width<=limit&&r.height<=limit;
}
}
unsigned screen_width(Screen side) noexcept {return side==Screen::top?400:side==Screen::bottom?320:0;}
bool valid(const ScreenClip& clip,unsigned width) noexcept {
    return width&&width<=400&&(!clip.enabled()||(clip.left<clip.right&&clip.top<clip.bottom&&clip.right<=width&&clip.bottom<=240));
}
Material screen_material() noexcept {
    Material value;value.cull=Cull::none;value.depth_test=value.depth_write=false;return value;
}
Error validate(const ScreenCanvas& canvas) noexcept {
    if((canvas.clear&255u)!=255u||canvas.brightness<-16||canvas.brightness>16||
       canvas.layers>ScreenCanvas::max_layers||(canvas.layers&&!canvas.layer))return Error::invalid_data;
    for(std::size_t i=0;i<canvas.layers;++i){const auto& layer=canvas.layer[i];
        if(!rectangle(layer.rectangle)||!valid(layer.clip)||!valid(layer.material)||layer.material.depth_test||layer.material.depth_write||
           layer.material.uv_mode!=UvMode::affine||layer.first_index%3||layer.index_count%3||
           (!layer.mesh&&(layer.first_index||layer.index_count)))return Error::invalid_data;
    }
    return Error::none;
}
Error screen_pose(Screen side,const ScreenRect& r,Matrix& output) noexcept {
    const auto width=screen_width(side);if(!width||!rectangle(r))return Error::invalid_data;
    Matrix result{};
    result.row[0][1]=-2*r.height/240;result.row[0][3]=1-2*r.y/240;
    result.row[1][0]=-2*r.width/width;result.row[1][3]=1-2*r.x/width;
    result.row[2][2]=-1;result.row[2][3]=-.5f;result.row[3][3]=1;
    output=result;return Error::none;
}
}
