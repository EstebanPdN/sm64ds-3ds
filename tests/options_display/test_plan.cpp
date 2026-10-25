#include "../../source/integrated_native/options/display_mode.hpp"
#include <cassert>
#include <cmath>
#include <set>
#include <cstdio>
int main(){
 unsigned checks=0;
 for(unsigned mode=0;mode<3;++mode)for(unsigned native=0;native<2;++native){
  const auto p=display_mode::plan(mode,native);
  assert(p.width==(mode==0?320u:400u));assert(p.height==240);checks+=2;
  assert(p.geometry_width==(native?p.width:256));assert(p.geometry_height==(native?240u:192u));checks+=2;
  // Independent circle oracle: x/y scales must agree for Original and Wide.
  const float px=.2f*p.projection_x*p.width*.5f,py=.2f*(4.f/3.f)*p.height*.5f;
  assert((std::abs(px-py)<.0001f)==(mode!=2));++checks;
  // Geometry outside originalXclip must actually enter the Wide viewport.
  assert((1.1f*p.projection_x<1.f)==(mode==1));++checks;
  for(unsigned x=0;x<400;++x){
   const auto q=display_mode::source_x(x,p);
   if(x<p.left || x>=p.left+p.width)assert(q==~0u);
   else assert(q==(x-p.left)*256/p.width && q<256);
   ++checks;
  }
  // Native viewport addresses distinct raster pixels for every LCD position.
  std::set<unsigned> samples;
  for(unsigned x=0;x<p.width;++x)samples.insert(x*p.geometry_width/p.width);
  assert(samples.size()==p.geometry_width);++checks;
  for(bool menu:{false,true})for(bool feedback:{false,true}){
   const auto f=display_mode::plan(mode,native,menu,feedback);
   assert(f.native==(native && !menu && !feedback));++checks;
  }
 }
 const auto native=display_mode::plan(1,1);
 assert(display_mode::eyes(native,false,true,false,1.f)==1);++checks;
 assert(display_mode::eyes(native,true,true,false,0.f)==1);++checks;
 assert(display_mode::eyes(native,true,true,false,1.f)==2);++checks;
 assert(display_mode::eyes(display_mode::plan(1,0),false,true,false,0.f)==2);++checks;
 assert(display_mode::resident_target_bytes(native,2)-display_mode::resident_target_bytes(native,1)==1310720u);++checks;
 for(unsigned y=0;y<240;++y){assert(display_mode::source_y(y)==y*4/5);++checks;}
 std::printf("Display plan: %u aspect, coverage, raster and pixel mapping checks passed\n",checks);
}
