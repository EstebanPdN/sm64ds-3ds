#include "screen_scene.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace sm64ds::render;
int main(){
    ScreenCanvas canvas;assert(validate(canvas)==Error::none);
    ScreenLayer layer;layer.rectangle={20,30,100,60};canvas.layer=&layer;canvas.layers=1;
    assert(validate(canvas)==Error::none);Matrix pose;
    layer.clip={7,13,301,233};assert(validate(canvas)==Error::none&&valid(layer.clip,320));
    layer.clip.right=321;assert(!valid(layer.clip,320)&&valid(layer.clip,400));
    layer.clip.bottom=241;assert(validate(canvas)==Error::invalid_data);
    layer.clip={0,0,0,1};assert(validate(canvas)==Error::invalid_data);layer.clip={};
    for(auto side:{Screen::top,Screen::bottom}){
        assert(screen_pose(side,layer.rectangle,pose)==Error::none);
        const float left=1-40.f/screen_width(side),right=1-240.f/screen_width(side);
        assert(std::fabs(pose.row[1][3]-left)<1e-6f&&std::fabs(pose.row[1][0]+pose.row[1][3]-right)<1e-6f);
        assert(pose.row[0][3]==.75f&&pose.row[0][1]+pose.row[0][3]==.25f&&pose.row[2][2]==-1&&pose.row[2][3]==-.5f&&pose.row[3][3]==1);
    }
    const auto before=pose;assert(screen_pose(static_cast<Screen>(2),layer.rectangle,pose)==Error::invalid_data);
    assert(pose.row[1][3]==before.row[1][3]);
    layer.rectangle.width=0;assert(validate(canvas)==Error::invalid_data);
    layer.rectangle.width=100;layer.rectangle.x=std::numeric_limits<float>::infinity();assert(validate(canvas)==Error::invalid_data);
    layer.rectangle.x=20;layer.material.depth_write=true;assert(validate(canvas)==Error::invalid_data);
    layer.material=screen_material();layer.material.uv_mode=UvMode::fixed;assert(validate(canvas)==Error::invalid_data);
    layer.material=screen_material();layer.first_index=3;assert(validate(canvas)==Error::invalid_data);
    layer.first_index=0;canvas.brightness=-17;assert(validate(canvas)==Error::invalid_data);
    canvas.brightness=16;assert(validate(canvas)==Error::none);
    canvas.brightness=0;canvas.clear&=~255u;assert(validate(canvas)==Error::invalid_data);
    canvas.clear=0xff;canvas.layers=65;assert(validate(canvas)==Error::invalid_data);
    canvas.layers=1;canvas.layer=nullptr;assert(validate(canvas)==Error::invalid_data);
}
