#include "enhance_overlay.h"
#include "native_lower_painter.hpp"
#include "rom_art.hpp"
#include "diagnostics_skin.hpp"
#include <cstdio>
#include <cstring>
#include <algorithm>
namespace enhance { namespace {
using loading_screen::Image;using loading_screen::Font;using loading_screen::Artwork;
const Artwork& art(){static rom_art::Resources resources;static const bool loaded=resources.load_embedded();(void)loaded;return resources.views();}
uint32_t color(uint16_t c){auto expand=[](unsigned n){return (n<<3)|(n>>2);};return 0xff000000u|(expand((c>>10)&31)<<16)|(expand((c>>5)&31)<<8)|expand(c&31);}
void pixel(uint32_t* p,int x,int y,uint16_t c){if(x>=0&&x<320&&y>=0&&y<240&&(c&0x8000))p[y*320+x]=color(c);}
void image(uint32_t* p,const Image& im,int x,int y,int w,int h){if(!im.valid()||w<=0||h<=0)return;for(int j=0;j<h;++j)for(int i=0;i<w;++i)pixel(p,x+i,y+j,im.pixels[unsigned(j)*im.height/unsigned(h)*im.width+unsigned(i)*im.width/unsigned(w)]);}
int width(const Font& f,const char* s){int n=0;for(;*s;++s)n+=f.advances?f.advances[uint8_t(*s)]:f.cell_width;return n;}
void text(uint32_t* p,const Font& f,int x,int y,const char* s){if(!f.valid())return;const unsigned columns=f.image.width/f.cell_width;for(;*s;++s){unsigned c=uint8_t(*s),g=f.glyphs[c];if(g!=0xffff)for(unsigned j=0;j<f.cell_height;++j)for(unsigned i=0;i<f.cell_width;++i)pixel(p,x+int(i),y+int(j),f.image.pixels[(g/columns*f.cell_height+j)*f.image.width+g%columns*f.cell_width+i]);x+=f.advances?f.advances[c]:f.cell_width;}}
void centered(uint32_t* p,const Font& f,int y,const char* s){text(p,f,(320-width(f,s))/2,y,s);}
}
int draw_game_text(uint32_t* p,int w,int h,int x,int y,const char* str,bool title_font){
 const Font& f=title_font?art().title_font:art().body_font;if(!p||!str||!f.valid())return 0;int start=x;const unsigned columns=f.image.width/f.cell_width;
 for(;*str;++str){unsigned c=uint8_t(*str),g=f.glyphs[c];if(g!=0xffff)for(unsigned j=0;j<f.cell_height;++j)for(unsigned i=0;i<f.cell_width;++i){int px=x+int(i),py=y+int(j);if(px<0||px>=w||py<0||py>=h)continue;auto ink=f.image.pixels[(g/columns*f.cell_height+j)*f.image.width+g%columns*f.cell_width+i];if(ink&0x8000)p[py*w+px]=color(ink);}x+=f.advances?f.advances[c]:f.cell_width;}return x-start;
}
void diagnostic_box(uint32_t* p,int x,int y,int w,int h){
 const int b=6;for(int j=0;j<h;++j)for(int i=0;i<w;++i){unsigned sx=i<b?i:i>=w-b?18+i-(w-b):6+(i-b)%12;unsigned sy=j<b?j:j>=h-b?18+j-(h-b):6+(j-b)%12;pixel(p,x+i,y+j,diagnostics_skin::frame[sy*24+sx]);}
}
void render_menu_background(uint32_t* p){
 if(!p)return;const auto& a=art();
 for(int y=0;y<240;++y)for(int x=0;x<320;++x)p[y*320+x]=color(a.pattern.pixels[unsigned(y)*a.pattern.height/240*a.pattern.width+unsigned(x)%a.pattern.width]);
}
void render_diagnostics(uint32_t* p,const Diagnostics& d){
 if(!p)return;const auto& a=art();render_menu_background(p);
 image(p,a.star,12,8,22,22);centered(p,a.title_font,12,"DIAGNOSTICS");
 diagnostic_box(p,8,43,112,91);diagnostic_box(p,127,43,185,91);diagnostic_box(p,8,141,304,58);
 text(p,a.body_font,16,49,"PERFORMANCE");text(p,a.body_font,135,49,"MEMORY");
 char v[48];std::snprintf(v,sizeof v,"%.1f FPS",d.fps);text(p,a.body_font,17,76,v);
 std::snprintf(v,sizeof v,"%.1f ms",d.frame_ms);text(p,a.body_font,17,105,v);
 std::snprintf(v,sizeof v,"Heap free: %u KB",d.heap_free/1024);text(p,a.body_font,135,77,v);
 std::snprintf(v,sizeof v,"Linear: %u KB",d.linear_free/1024);text(p,a.body_font,135,105,v);
 text(p,a.body_font,17,149,d.new_model?"New 3DS":"Old 3DS");std::snprintf(v,sizeof v,"%u x %u%s",d.render_width,d.render_height,d.fallback?" DS":"");text(p,a.body_font,135,149,v);
 text(p,a.body_font,17,176,d.dumps_enabled?"Full RAM: On    L / R / A":"Full RAM: Off");
 diagnostic_box(p,105,211,110,25);centered(p,a.body_font,215,"SELECT  Close");
 struct VersionCanvas {uint32_t* pixels;void pixel(int x,int y,uint32_t color){pixels[y*320+x]=color;}} canvas{p};
 native_lower::version(canvas);
}
int draw_text(uint32_t* p,int w,int h,int x,int y,const char* s,uint32_t c,const uint8_t* f,int scale){if(!p||!s||!f||scale<=0)return 0;int start=x;for(;*s;++s,x+=8*scale)for(int j=0;j<8;++j)for(int i=0;i<8;++i)if(f[uint8_t(*s)*8+j]&(0x80>>i))for(int yy=0;yy<scale;++yy)for(int xx=0;xx<scale;++xx){int px=x+i*scale+xx,py=y+j*scale+yy;if(px>=0&&px<w&&py>=0&&py<h)p[py*w+px]=c;}return x-start;}
}
