#include "sm64ds/game_input.hpp"
#include <algorithm>
#include <cmath>

namespace sm64ds {
void GameInputLatch::sample(const InputFrame& input) {
    latest_=input;
    pending_press_|=input.pressed;
    pending_release_|=input.released;
    if(input.touched && !previous_touch_) {
        touch_edge_=true;edge_x_=input.touch_x;edge_y_=input.touch_y;
    }
    previous_touch_=input.touched;
}
GameInputSample GameInputLatch::consume() {
    GameInputSample out;
    out.frame=latest_;
    out.frame.pressed=pending_press_;out.frame.released=pending_release_;
    // libctru's twelve DS buttons occupy bits 0..11 in the same order.
    // ZL/ZR, Circle Pad direction bits and system events cannot leak into them.
    out.pad={uint16_t(latest_.held&0x0fff),uint16_t(pending_press_&0x0fff)};
    const bool down=latest_.touched||touch_edge_;
    // The original second byte marks either transition, including release;
    // CheckInput uses (!touched && tapped) to start its five-tick touch timer.
    out.touch={uint8_t(down),uint8_t(touch_edge_ || down!=delivered_touch_),
        uint8_t(std::min<uint16_t>(touch_edge_?edge_x_:latest_.touch_x,255)),
        uint8_t(std::min<uint16_t>(touch_edge_?edge_y_:latest_.touch_y,191))};
    delivered_touch_=down;
    pending_press_=pending_release_=0;touch_edge_=false;
    return out;
}
void GameInputLatch::reset() { *this={}; }

bool game_accepts_movement(uint8_t mode,uint32_t flags,uint8_t pause,int32_t script) {
    // Exact main_part gate in the original Stage::CheckInput, including mode 2.
    return mode==1 || (mode!=2 && (flags&0x80) && (pause!=0 || script==0));
}
bool apply_game_analog(GameControl& control,Stick stick,GameAtan2 atan2) {
    if(!atan2 || !std::isfinite(stick.x) || !std::isfinite(stick.y))return false;
    float length=std::sqrt(stick.x*stick.x+stick.y*stick.y);
    if(!std::isfinite(length) || length<=0)return false;
    float scale=4096.0f/std::max(1.0f,length);
    const int x=int(std::lround(stick.x*scale));
    const int y=int(std::lround(-stick.y*scale)); // DS touch coordinates grow down.
    control.magnitude=int16_t(std::lround(std::min(1.0f,length)*4096.0f));
    control.nx=int16_t(x);control.ny=int16_t(y);
    control.angle=atan2(x,y);
    control.touching=1;
    return true;
}
}
