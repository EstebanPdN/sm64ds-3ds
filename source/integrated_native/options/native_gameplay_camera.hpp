#pragma once
#include "native_gameplay_camera_art.hpp"
#include "stylus_camera.hpp"
#include <algorithm>
#include <cstdint>

namespace native_gameplay_camera {
// Only the approved original arrows belong to this overlay. The ROM still
// draws its own counters, portraits, map and other HUD objects.
inline constexpr int width=320,height=240,size=32;
inline constexpr unsigned left=0x200,right=0x100;
inline constexpr uint64_t image_key=~uint64_t(1);
struct Rect {
 int x,y;
 constexpr bool contains(int px,int py)const{return px>=x&&px<x+size&&py>=y&&py<y+size;}
};
inline constexpr Rect left_rect{10,198},right_rect{278,198};
constexpr unsigned hit(int x,int y){return left_rect.contains(x,y)?left:right_rect.contains(x,y)?right:0;}

inline void render(uint32_t* pixels){
 std::fill_n(pixels,width*height,0u);
 const Rect positions[]={left_rect,right_rect};
 const uint32_t* images[]={art::left,art::right};
 for(unsigned i=0;i<2;++i)for(int y=0;y<size;++y)
  std::copy_n(images[i]+y*size,size,pixels+(positions[i].y+y)*width+positions[i].x);
}

// A press chooses its owner once. A stroke entering a button cannot press it;
// leaving an arrow cannot turn into touch movement or a free-camera stroke.
class Touch {
 enum class Owner { Retail,Left,Right,Stroke,Blocked };
 Owner owner_=Owner::Retail;
 bool down_=false;
 unsigned buttons_=0,pressed_=0;
 cstick_camera::StylusStroke stroke_;
 bool camera_owned()const{return owner_!=Owner::Retail;}
public:
 bool sample(bool enabled,bool buttons_visible,bool old_model,bool down,int x,int y){
  const bool consumed=camera_owned();
  const unsigned previous=buttons_;buttons_=0;
  if(!down){down_=false;owner_=Owner::Retail;pressed_=0;stroke_.reset();return consumed;}
  if(!down_){
   owner_=Owner::Retail;
   if(enabled){
    const unsigned button=buttons_visible?hit(x,y):0;
    owner_=button==left?Owner::Left:button==right?Owner::Right:old_model?Owner::Stroke:Owner::Retail;
   }
  }
  down_=true;
  if(!enabled&&camera_owned())owner_=Owner::Blocked;
  if((owner_==Owner::Left||owner_==Owner::Right)&&!buttons_visible)owner_=Owner::Blocked;
  if(owner_==Owner::Stroke&&!old_model)owner_=Owner::Blocked;
  if(owner_==Owner::Left&&hit(x,y)==left)buttons_=left;
  if(owner_==Owner::Right&&hit(x,y)==right)buttons_=right;
  if(owner_==Owner::Stroke&&!(buttons_visible&&hit(x,y)))stroke_.sample(true,x,y);
  else stroke_.reset();
  pressed_=(pressed_|(buttons_&~previous))&buttons_;
  return camera_owned();
 }
 unsigned buttons()const{return buttons_;}
 unsigned take_pressed(){const auto value=pressed_;pressed_=0;return value;}
 void consume_stroke(int& x,int& y){stroke_.consume(x,y);}
};
}
