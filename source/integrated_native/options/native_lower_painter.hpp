#pragma once
#include "native_lower_art.hpp"
#include "build_version.hpp"
#include <cstdint>
namespace native_lower {
template<class Sink> void frame(Sink& out,int x,int y,int width,int height,uint32_t fill=0xfffffbd9){
 for(int yy=0;yy<height;++yy)for(int xx=0;xx<width;++xx){
  const int u=xx<6?xx:xx>=width-6?24-width+xx:6+(xx-6)%12;
  const int v=yy<6?yy:yy>=height-6?24-height+yy:6+(yy-6)%12;
  uint32_t c=native_lower_art::frame[v*24+u];if(c==0xfffffbd9)c=fill;if(c>>24)out.pixel(x+xx,y+yy,c);
 }
}
inline int text_width(const char* s,bool title=false){int n=0;for(;s&&*s;++s){unsigned c=uint8_t(*s);if(c<32||c>126)c='?';n+=title?native_lower_art::title_advance[c-32]:native_lower_art::body_advance[c-32];}return n;}
template<class Sink> void text(Sink& out,int x,int y,const char* s,uint32_t color,bool title=false,bool green=false){
 for(;s&&*s;++s){unsigned c=uint8_t(*s);if(c<32||c>126)c='?';c-=32;
  for(unsigned yy=0;yy<16;++yy){unsigned row=title?native_lower_art::title[c][yy]:native_lower_art::body[c][yy];
   for(unsigned xx=0;xx<(title?16u:8u);++xx)if(row&(1u<<xx)){uint32_t ink=color;if(title){unsigned index=yy*16+xx;ink=native_lower_art::title_colors[(native_lower_art::title_pixels[c][index/2]>>((index%2)*4))&15];}if(title&&green){if(ink==0xffde2929)ink=0xff529400;else if(ink==0xffee7318)ink=0xff80bf21;else if(ink==0xfff6bd10)ink=0xffb6df42;}out.pixel(x+int(xx),y+int(yy),ink);}
  }x+=title?native_lower_art::title_advance[c]:native_lower_art::body_advance[c];
 }
}
template<class Sink> void centered(Sink& out,int y,const char* s,uint32_t color,bool title=false){text(out,(320-text_width(s,title))/2,y,s,color,title);}
// Compact 5x7 build badge fits entirely below the pause root's final button
// (ends at y=229). Keep retail menu glyphs/positions unchanged. This tiny
// fixed alphabet never allocates and is painted only when a UI image changes.
template<class Sink> void version(Sink& out){
 constexpr char alphabet[]="BUILD0123456789";
 constexpr uint8_t glyphs[][7]={
  {30,17,17,30,17,17,30},{17,17,17,17,17,17,14},
  {14,4,4,4,4,4,14},{16,16,16,16,16,16,31},{30,17,17,17,17,17,30},
  {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
  {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
  {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
  {14,17,17,15,1,1,14}
 };
 int x=6;
 for(const char* p=sm64ds_build::label;*p;++p){
  unsigned g=0;while(alphabet[g]&&alphabet[g]!=*p)++g;
  if(!alphabet[g]){x+=4;continue;}
  for(int y=0;y<7;++y)for(int c=0;c<5;++c)if(glyphs[g][y]&(16>>c))out.pixel(x+c+1,232+y,0xff000000);
  for(int y=0;y<7;++y)for(int c=0;c<5;++c)if(glyphs[g][y]&(16>>c))out.pixel(x+c,231+y,0xffffffff);
  x+=6;
 }
}
// Dump progress card (56..184): title, subtitle, bar and warning share
// equal gaps, with the same 14-16 pixel margins at the top and bottom.
template<class Sink> void dump(Sink& out,unsigned percent,bool failed=false,bool full=true){
 if(percent>100)percent=100;
 frame(out,16,56,288,128);
 centered(out,70,failed?"DUMP ERROR":"DUMPING",0xff8d4300,true);
 centered(out,98,failed?"Unable to finish the capture":full?"Saving screens, RAM and VRAM":"Saving screens and VRAM",0xff8d4300);
 for(int y=126;y<144;++y)for(int x=32;x<288;++x){uint32_t c=(x==32||x==287||y==126||y==143)?0xff191008:0xff8d4300;if(y>128&&y<141&&x>34&&unsigned(x-35)<250*percent/100)c=y<134?0xffffeb5a:0xffffad00;out.pixel(x,y,c);}
 centered(out,152,"Do not turn off the console",0xff8d4300);
}
}
