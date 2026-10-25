#pragma once
#include <cmath>
#include <cstdint>
namespace sm64ds {
struct Stick { float x=0,y=0; };
inline Stick normalize_stick(int x,int y,float deadzone=0.12f,float range=156.0f) {
    float nx=x/range,ny=y/range,len=std::sqrt(nx*nx+ny*ny);
    if(len<=deadzone)return {};
    float magnitude=(std::fmin(len,1.0f)-deadzone)/(1.0f-deadzone);
    return {nx/len*magnitude,ny/len*magnitude};
}
struct InputFrame {
    Stick movement,camera;
    uint32_t held=0,pressed=0,released=0;
    bool touched=false,new3ds=false;
    uint16_t touch_x=0,touch_y=0;
};
// Bottom screen: 320x240, DS touch space: 256x192; same 4:3 aspect.
inline uint16_t touch_to_ds(unsigned v,unsigned extent,unsigned ds_extent) {
    return uint16_t(std::fmin(v,extent-1)*ds_extent/extent);
}
#ifdef __3DS__
bool input_init();
InputFrame input_poll(bool dpad_camera_fallback=true);
void input_exit();
#endif
}
