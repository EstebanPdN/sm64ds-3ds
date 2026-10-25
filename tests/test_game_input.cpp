#include "sm64ds/game_input.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace sm64ds;
static int ax,ay;
static int16_t angle(int x,int y) {ax=x;ay=y;return 1234;}
int main() {
    GameInputLatch input;InputFrame frame;
    frame.held=frame.pressed=(1<<1)|(1<<14); // B and ZL; only B belongs to DS.
    input.sample(frame);
    frame.held=frame.pressed=0;frame.released=1<<1;input.sample(frame);
    auto tick=input.consume();assert(tick.pad.held==0 && tick.pad.pressed==2);
    assert(input.consume().pad.pressed==0);
    frame={};frame.touched=true;frame.touch_x=140;frame.touch_y=100;input.sample(frame);
    frame.touched=false;input.sample(frame);
    tick=input.consume();assert(tick.touch.touched && tick.touch.tapped);
    assert(tick.touch.x==140 && tick.touch.y==100);
    tick=input.consume();assert(!tick.touch.touched && tick.touch.tapped);
    assert(!input.consume().touch.tapped);
    frame.touched=true;input.sample(frame);assert(input.consume().touch.tapped);
    input.sample(frame);assert(!input.consume().touch.tapped);
    frame.touched=false;input.sample(frame);assert(input.consume().touch.tapped);
    input.reset();assert(input.consume().pad.held==0);
    // Cutscenes and mode 2 must retain the original gate's refusal.
    assert(game_accepts_movement(0,0x80,0,0));
    assert(!game_accepts_movement(0,0,0,0));
    assert(!game_accepts_movement(0,0x80,0,1));
    assert(game_accepts_movement(0,0x80,1,1));
    assert(game_accepts_movement(1,0,0,1));
    assert(!game_accepts_movement(2,0x80,1,0));
    GameControl control{};control.held=0xa55a;control.pressed=0x55aa;control.mode=2;
    assert(apply_game_analog(control,{0,0.5f},angle));
    assert(control.magnitude==2048 && control.nx==0 && control.ny==-2048);
    assert(ax==0 && ay==-2048 && control.angle==1234 && control.touching);
    assert(control.held==0xa55a && control.pressed==0x55aa && control.mode==2);
    assert(apply_game_analog(control,{1,1},angle));
    assert(control.magnitude==4096 && control.nx==2896 && control.ny==-2896);
    assert(!apply_game_analog(control,{},angle));
    assert(!apply_game_analog(control,{NAN,0},angle));
    assert(!apply_game_analog(control,{INFINITY,0},angle));
}
