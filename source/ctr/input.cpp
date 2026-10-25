#include <3ds.h>
#include "sm64ds/input.hpp"
namespace sm64ds {
namespace { bool is_new=false,irrst=false; }
bool input_init() {
    is_new=false;APT_CheckNew3DS(&is_new);
    if(is_new)irrst=R_SUCCEEDED(irrstInit());
    return is_new;
}
InputFrame input_poll(bool dpad_camera_fallback) {
    hidScanInput();InputFrame f;
    f.held=hidKeysHeld();f.pressed=hidKeysDown();f.released=hidKeysUp();f.new3ds=is_new;
    circlePosition left{};hidCircleRead(&left);
    f.movement=normalize_stick(left.dx,left.dy);
    if(irrst) {circlePosition right{};irrstScanInput();irrstCstickRead(&right);f.camera=normalize_stick(right.dx,right.dy,0.08f);}
    if(dpad_camera_fallback && f.camera.x==0 && f.camera.y==0) {
        f.camera.x=float(bool(f.held&KEY_DRIGHT))-float(bool(f.held&KEY_DLEFT));
        f.camera.y=float(bool(f.held&KEY_DUP))-float(bool(f.held&KEY_DDOWN));
    }
    f.touched=(f.held&KEY_TOUCH)!=0;
    if(f.touched) {touchPosition p{};hidTouchRead(&p);f.touch_x=touch_to_ds(p.px,320,256);f.touch_y=touch_to_ds(p.py,240,192);}
    return f;
}
void input_exit() {if(irrst)irrstExit();irrst=false;}
}
