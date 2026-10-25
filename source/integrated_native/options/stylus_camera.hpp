#pragma once
#include <algorithm>
namespace cstick_camera {
class StylusStroke {
 bool down_=false;int x_=0,y_=0,dx_=0,dy_=0;
public:
 void reset(){down_=false;dx_=dy_=0;}
 void sample(bool down,int x,int y){
  if(!down){reset();return;}
  x=std::clamp(x,0,319);y=std::clamp(y,0,239);
  // Accumulate net displacement before rate limiting. Clamping each HID
  // segment makes an out-and-back stroke rotate in the opposite direction.
  if(down_){dx_+=x-x_;dy_+=y-y_;}
  down_=true;x_=x;y_=y;
 }
 void consume(int& x,int& y){x=std::clamp(dx_,-32,32);y=-std::clamp(dy_,-32,32);dx_=dy_=0;}
};
}
