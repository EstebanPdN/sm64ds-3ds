#pragma once
#include <cstdint>
namespace display_mode {
enum class Framing : uint8_t { Original=0, Wide=1, Stretch=2 };
struct Plan {
 unsigned width,height,left,geometry_width,geometry_height;
 float projection_x;
 bool native;
};
// Native applies to geometry and its composed upper panel, not DS HUD assets.
// Captures/window feedback and multipass DS-alpha effects retain DS raster rules.
constexpr Plan plan(uint8_t mode,uint8_t resolution,bool menu=false,bool ds_feedback=false){
 const auto framing=mode<3?Framing(mode):Framing::Original;
 const bool original=menu || framing==Framing::Original;
 const bool native=resolution==1 && !menu && !ds_feedback;
 return {original?320u:400u,240u,original?40u:0u,
         native?(original?320u:400u):256u,native?240u:192u,
         !menu && framing==Framing::Wide?.8f:1.f,native};
}
constexpr unsigned texture_width(const Plan& p){return p.native?512u:256u;}
constexpr unsigned eyes(const Plan& p,bool stereo,bool upper,bool delayed,float slider){
 return p.native && !(stereo && upper && !delayed && slider>0.f)?1u:2u;
}
// Geometry RGBA8+depth24stencil8, composed upper eyes and remaining DS panels.
constexpr unsigned resident_target_bytes(const Plan& p,unsigned eye_count){
 return eye_count*texture_width(p)*256u*8u +
   (eye_count*texture_width(p)+(3u-eye_count)*256u)*256u*4u;
}
constexpr unsigned source_x(unsigned lcd_x,const Plan& p){
 return lcd_x<p.left || lcd_x>=p.left+p.width?~0u:(lcd_x-p.left)*256u/p.width;
}
constexpr unsigned source_y(unsigned lcd_y){return lcd_y*192u/240u;}
} // namespace display_mode
