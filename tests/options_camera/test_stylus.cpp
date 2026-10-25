#include "../../source/integrated_native/options/stylus_camera.hpp"
#include <cassert>
int main(){
 cstick_camera::StylusStroke s;int x=99,y=99;
 s.sample(true,100,100);s.consume(x,y);assert(!x&&!y);
 s.sample(true,105,95);s.sample(true,108,92);s.consume(x,y);assert(x==8&&y==8);
 s.consume(x,y);assert(!x&&!y);
 s.sample(true,109,93);s.consume(x,y);assert(x==1&&y==-1);
 s.sample(false,0,0);s.sample(true,310,220);s.consume(x,y);assert(!x&&!y);
 s.sample(true,0,0);s.consume(x,y);assert(x==-32&&y==32);
 s.sample(true,5,5);s.reset();s.consume(x,y);assert(!x&&!y);
 // HID may run several times before the DS camera update. Saturation belongs
 // to consumption, so intermediate overshoot cannot reverse the net gesture.
 for(int sign:{-1,1}){
  s.reset();s.sample(true,160,120);
  s.sample(true,160+sign*100,120+sign*100);
  s.sample(true,160,120);s.consume(x,y);assert(!x&&!y);
  s.sample(true,160+sign*100,120+sign*100);
  s.sample(true,160+sign*10,120+sign*10);
  s.consume(x,y);assert(x==sign*10&&y==-sign*10);
  s.consume(x,y);assert(!x&&!y);
 }
 // Invalid edge samples stay within the native touch surface.
 s.reset();s.sample(true,-100,-100);s.sample(true,1000,1000);
 s.consume(x,y);assert(x==32&&y==-32);
 s.sample(true,319,239);s.consume(x,y);assert(!x&&!y);
 s.sample(true,300,220);s.sample(false,0,0);s.consume(x,y);assert(!x&&!y);
}
