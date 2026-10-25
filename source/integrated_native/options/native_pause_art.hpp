#pragma once
#include "native_lower_painter.hpp"
#include "retail_pause_art.hpp"
#include "option_label_art.hpp"
#include "native_pause_menu.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace native_lower {
struct Canvas {
 uint32_t* pixels;
 void pixel(int x,int y,uint32_t c){if(x>=0&&y>=0&&x<320&&y<240)pixels[y*320+x]=c;}
};
template<class Sink> void arrow(Sink& out,int x,int y,bool right){
 const uint32_t* pixels=right?retail_pause_art::arrow_right:retail_pause_art::arrow_left;
 for(unsigned yy=0;yy<retail_pause_art::arrow_left_height;++yy)
  for(unsigned xx=0;xx<retail_pause_art::arrow_left_width;++xx){
   uint32_t c=pixels[yy*retail_pause_art::arrow_left_width+xx];
   if(c>>24)out.pixel(x+int(xx),y+int(yy),c);
  }
}
template<class Sink> void star(Sink& out,int x,int y){
 for(unsigned yy=0;yy<retail_pause_art::selection_star_height;++yy)
  for(unsigned xx=0;xx<retail_pause_art::selection_star_width;++xx){
   uint32_t c=retail_pause_art::selection_star[yy*retail_pause_art::selection_star_width+xx];
   if(c>>24)out.pixel(x+int(xx),y+int(yy),c);
  }
}
template<class Sink> void retail_frame(Sink& out,int x,int y,int width,int height,const uint32_t* sprite,int sw,int sh){
 // Copy corners and bevels verbatim; extend only the flat interior.
 constexpr int edge=12;
 for(int yy=0;yy<height;++yy)for(int xx=0;xx<width;++xx){
  int u=xx<edge?xx:xx>=width-edge?sw-width+xx:sw/2;
  int v=yy<edge?yy:yy>=height-edge?sh-height+yy:sh/2;
  uint32_t c=sprite[v*sw+u];if(c>>24)out.pixel(x+xx,y+yy,c);
 }
}
// Label centred at 1x (pixel-perfect: the retail font is never resampled).
template<class Sink> void retail_text(Sink& out,int x,int y,int width,int height,const char* label,bool title){
 const int tw=text_width(label,title),th=16;
 text(out,x+(width-tw)/2,y+(height-th)/2,label,0xff8d4300,title,true);
}
template<class Sink> void button(Sink& out,int x,int y,int width,int height,const char* label,bool selected,bool orange=false){
 const uint32_t* sprite=orange?retail_pause_art::ok:selected?retail_pause_art::selected:retail_pause_art::normal;
 retail_frame(out,x,y,width,height,sprite,orange?int(retail_pause_art::ok_width):200,orange?int(retail_pause_art::ok_height):40);
 char uppercase[128];unsigned i=0;
 for(;label[i]&&i+1<sizeof uppercase;++i)uppercase[i]=label[i]>='a'&&label[i]<='z'?label[i]-'a'+'A':label[i];
 uppercase[i]=0;
 retail_text(out,x,y,width,height,uppercase,orange);
}
template<class Sink> void action_button(Sink& out,int x,int y,int width,int height,const char* label){
 retail_frame(out,x,y,width,height,retail_pause_art::action,retail_pause_art::action_width,retail_pause_art::action_height);
 retail_text(out,x,y,width,height,label,true);
}
template<class Sink> void dump_shortcut(Sink& out,int x,int y,int width,int height){
 constexpr int gap=2,pw=retail_pause_art::shortcut_plus_width;
 const int total=text_width("LRA")+2*(pw+2*gap),top=y+(height-16)/2;
 int left=x+(width-total)/2;
 const char* letters[]={"L","R","A"};
 for(unsigned i=0;i<3;++i){
  text(out,left,top,letters[i],0xff8d4300);left+=text_width(letters[i]);
  if(i==2)break;
  left+=gap;
  for(unsigned v=0;v<retail_pause_art::shortcut_plus_height;++v)for(unsigned u=0;u<retail_pause_art::shortcut_plus_width;++u){
   const uint32_t c=retail_pause_art::shortcut_plus[v*pw+u];
   if(c>>24)out.pixel(left+int(u),top+5+int(v),c);
  }
  left+=pw+gap;
 }
}
template<class Sink> void root(Sink& out,unsigned selected,unsigned count,const uint16_t* positions){
 const char* labels[]={"CONTINUE","CONTROLLER","OPTIONS","QUIT"};count=std::min(count,4u);
 for(unsigned i=0;i<count;++i){const int y=int(positions[i]/32)*10;button(out,10,y,300,40,labels[i],selected==i,true);if(selected==i)star(out,20,y+12);}
 version(out);
}
// Fixed native pixels: original menu glyphs plus documented straight extensions.
template<class Sink> void option_label(Sink& out,int x,int y,const char* label,bool selected=false){
 for(;*label;++label){
  if(*label<'A'||*label>'Z'){x+=5;continue;}
  unsigned c=*label-'A',w=option_label_art::widths[c];
  for(unsigned v=0;v<14;++v)for(unsigned u=0;u<w;++u){
   unsigned ink=option_label_art::glyphs[c][v*w+u];
   if(ink)out.pixel(x+int(u),y+int(v),ink==1?0xff000000:selected?(v<4?0xffffffff:v<7?0xffffff63:v<10?0xffffc65a:0xffff8452):0xffffffff);
  }
  x+=int(w)-1;
 }
}
template<class Sink> void menu(Sink& out,const enhance::native_pause::Menu& model){
 using enhance::native_pause::Page;
 const auto page=model.page();
 const char* title=page==Page::Options?"OPTIONS":page==Page::Controller?"CONTROLLER":page==Page::Audio?"AUDIO":page==Page::Display?"DISPLAY":page==Page::Developer?"DEVELOPER":page==Page::ConfirmDelete?"DELETE DUMPS?":"UPDATES";
 centered(out,page==Page::ConfirmDelete?50:10,title,0xffffffff,true);
 const unsigned first=model.first(),count=model.count(),visible=std::min(3u,count-first);
 for(unsigned r=0;r<visible;++r){
  const unsigned row=first+r;char value[48];model.value(row,value,sizeof value);
  for(char* p=value;*p;++p)if(*p>='a'&&*p<='z')*p-=('a'-'A');
  const int y=36+int(r)*44;const bool active=model.selected()==row;
  if(page==Page::ConfirmDelete){
   using namespace enhance::native_pause::layout;
   const int x=row==0?confirm_left:confirm_right;
   action_button(out,x,confirm_y,confirm_width,confirm_height,row==0?"KEEP":"DELETE");
   if(active)star(out,x-18,confirm_y+12);
   continue;
  }
  if(active)star(out,14,y+12);
  const char* label="";
  if(page==Page::Options){
   const char* names[]={"CONTROLLER","AUDIO","DISPLAY","DEVELOPER","UPDATE"};
   action_button(out,32,y,272,36,names[row+(count==4?1:0)]);
   continue;
  }
  const char* second=nullptr;
  if(page==Page::Controller){label=enhance::controller_options::art_label(row);second=enhance::controller_options::art_second_line(row);}
  if(page==Page::Audio)label=row==0?"SOUND":"VOLUME";
  if(page==Page::Display)label=row==0?"RESOLUTION":row==1?"SCREEN":"LOCK FPS";
  if(page==Page::Developer){const char* names[]={"FULL DUMPS","DUMP RAM","CLEAN DUMPS","SHOW FPS","OVERLAY"};label=names[row];}
  option_label(out,36,y+(second?1:12),label,active);
  if(second)option_label(out,36,y+18,second);
  const bool toggle=model.toggle_row(row);
  const bool audio=page==Page::Audio&&row==1;
  if(toggle){
   const bool on=std::strcmp(value,"ON")==0;
   for(unsigned b=0;b<2;++b){
    struct Tint {Sink& target;bool pressed;void pixel(int x,int y,uint32_t c){if(pressed&&c==0xffffecaf)c=0xffc9ac95;target.pixel(x,y,c);}} tint{out,b==unsigned(!on)};
    button(tint,155+int(b)*75,y,65,36,b?"OFF":"ON",false);
    if(b==unsigned(!on)){for(int x=167+int(b)*75;x<208+int(b)*75;++x)out.pixel(x,y+29,0xff8d4300);}
   }
  }else if(audio){
   button(out,155,y,137,36,"",false);
   char percent[12];std::snprintf(percent,sizeof percent,"%u%%",unsigned(model.settings().audio_volume));
   text(out,280-text_width(percent),y+12,percent,0xff8d4300);
   const int left=167,right=236,top=y+16,end=left+(right-left)*model.settings().audio_volume/200;
   for(int yy=top;yy<top+10;++yy)for(int xx=left;xx<right;++xx)out.pixel(xx,yy,xx==left||xx==right-1||yy==top||yy==top+9?0xff191008:xx<=end?0xffffc34e:0xff8d4300);
   frame(out,end-4,top-4,9,18,0xffffc34e);
  }else if(page==Page::Developer&&row==2){
   action_button(out,155,y,137,36,"CLEAN");
  }else{
   const bool shortcut=page==Page::Developer&&row==1&&model.settings().developer_dumps;
   button(out,155,y,137,36,shortcut?"":value,active);
   if(shortcut)dump_shortcut(out,155,y,137,36);
   arrow(out,142,y+11,false);arrow(out,298,y+11,true);
  }
 }
 if(model.page()==Page::Display){
  // Below the third row (124..160) and above the footer (198).
  centered(out,164,model.settings().res3d==enhance::Res3d::Native?
    (model.settings().screen_mode==0?"Native: 320 x 240":"Native: 400 x 240"):"Original DS: 256 x 192",0xfffffbd9);
  centered(out,180,"Changes apply when you resume",0xfffffbd9);
 }
 if(model.page()==Page::Update){
  // Visual placeholder only. No release status is claimed without a query.
  retail_frame(out,20,40,280,88,retail_pause_art::selected,200,40);
  centered(out,51,"INSTALLED VERSION",0xff8d4300);
  centered(out,72,sm64ds_build::label,0xff8d4300);
  centered(out,103,"GITHUB: NOT CONNECTED",0xff8d4300);
  retail_frame(out,40,137,240,36,retail_pause_art::normal,200,40);
  // Noninteractive: Update has zero selectable rows until the updater exists.
  centered(out,147,"CHECK FOR UPDATES",0xff9b8c64);
  centered(out,174,"AVAILABLE IN A FUTURE VERSION",0xfffffbd9);
 }
 if(count>3){
  for(unsigned y=0;y<retail_pause_art::page_down_height;++y)for(unsigned x=0;x<retail_pause_art::page_down_width;++x){
   uint32_t c=retail_pause_art::page_down[y*retail_pause_art::page_down_width+x];
   if(c>>24)out.pixel(149+int(x),174+int(y),c);
  }
 }
 if(page!=Page::ConfirmDelete)for(unsigned y=0;y<retail_pause_art::confirm_height;++y)for(unsigned x=0;x<retail_pause_art::confirm_width;++x){
  uint32_t c=retail_pause_art::confirm[y*retail_pause_art::confirm_width+x];if(c>>24)out.pixel(128+int(x),198+int(y),c);
 }
 for(unsigned yy=0;yy<retail_pause_art::back_height;++yy)for(unsigned xx=0;xx<retail_pause_art::back_width;++xx){
  uint32_t c=retail_pause_art::back[yy*retail_pause_art::back_width+xx];if(c>>24)out.pixel(278+xx,198+yy,c);
 }
 if(model.selected()==count)star(out,page==Page::ConfirmDelete||model.footer()?257:108,208);
 version(out);
}
}
