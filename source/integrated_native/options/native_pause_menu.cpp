#include "native_pause_menu.h"
#include "enhance_overlay.h"
#include "volume_slider.hpp"
#include "controller_preview.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace enhance::native_pause {
bool VramBackup::capture(uint8_t* chars,std::size_t chars_size,uint16_t* bg0,std::size_t bg0_entries,uint16_t* bg1,std::size_t bg1_entries){
 if(valid_)return true;
 if(!chars||!bg0||!bg1||chars_size<sizeof character_bytes_||bg0_entries<1024||bg1_entries<1024)return false;
 chars_=chars;bg0_=bg0;bg1_=bg1;std::memcpy(character_bytes_,chars,sizeof character_bytes_);std::memcpy(bg0_map_,bg0,sizeof bg0_map_);std::memcpy(bg1_map_,bg1,sizeof bg1_map_);valid_=true;return true;
}
bool VramBackup::restore(){
 if(!valid_)return false;
 std::memcpy(chars_,character_bytes_,sizeof character_bytes_);std::memcpy(bg0_,bg0_map_,sizeof bg0_map_);std::memcpy(bg1_,bg1_map_,sizeof bg1_map_);valid_=false;return true;
}
unsigned encode(const char* text,uint8_t* out,unsigned cap){if(!out||!cap)return 0;unsigned n=0;for(;text&&*text&&n+1<cap;++text){const unsigned c=uint8_t(*text);out[n++]=c>='0'&&c<='9'?c-'0':c>='A'&&c<='Z'?c-'A'+0x0a:c>='a'&&c<='z'?c-'a'+0x2d:c==' '?0x4d:c=='-'?0x47:c=='.'?0x48:c==':'?0x4a:c=='/'?0x4e:c=='&'?0x4c:c=='!'?0x27:c=='%'?native_volume_sprite::percent_code:0x26;}out[n]=0xff;return n;}
void Menu::begin(const Settings& s,unsigned sound,bool controller){
 settings_=original_=s;sound_=original_sound_=sound%3;
 selected_=footer_=0;requests_=0;finished_=accepted_=false;
 volume_dragging_=false;general_=false;direct_controller_=controller;
 page_=controller?Page::Controller:Page::Options;
}
unsigned Menu::count()const{
 switch(page_){
 case Page::Options:return general_?5:4;
 case Page::Controller:return controller_options::count;
 case Page::Audio:return 2;
 case Page::Display:return 3;
 case Page::Developer:return 5;
 case Page::ConfirmDelete:return 2;
 case Page::Update:return 0;
 }
 return 0;
}
namespace {
constexpr const char* option_pages[]={"Controller","Audio","Display","Developer","Update"};
constexpr const char* developer_rows[]={"Full RAM dumps","Dump RAM","Clean Dumps","Show FPS","Select overlay"};
}
const char* Menu::label(unsigned i)const{
 if(i>=count())return "";
 switch(page_){
 case Page::Options:return option_pages[i+(general_?0:1)];
 case Page::Controller:return controller_options::label(i);
 case Page::Audio:return i==0?"Sound mode":"Volume";
 case Page::Display:return i==0?"Resolution":i==1?"Screen mode":"Lock FPS";
 case Page::Developer:return developer_rows[i];
 case Page::ConfirmDelete:return i==0?"Keep":"Delete";
 case Page::Update:return "";
 }
 return "";
}
bool Menu::toggle_row(unsigned row)const{
 if(page_==Page::Controller)return controller_options::toggle(row);
 return page_==Page::Developer&&(row==0||row==3||row==4);
}
bool Menu::toggle_value(unsigned row)const{
 if(page_==Page::Controller)return controller_options::toggle_on(settings_,row);
 return row==0?settings_.developer_dumps:row==3?settings_.show_fps:settings_.developer_overlay;
}
void Menu::set_toggle(unsigned row,bool on){
 if(page_==Page::Controller){controller_options::set_toggle(settings_,row,on);return;}
 if(row==0)settings_.developer_dumps=on;
 else if(row==3)settings_.show_fps=on;
 else if(row==4)settings_.developer_overlay=on;
}
void Menu::value(unsigned i,char* out,unsigned cap)const{
 if(!cap)return;
 out[0]=0;
 if(i>=count())return;
 const char* text="";
 switch(page_){
 case Page::Options:case Page::ConfirmDelete:case Page::Update:break;
 case Page::Audio:{
  if(i==1){std::snprintf(out,cap,"%u%%",unsigned(settings_.audio_volume));return;}
  constexpr const char* modes[]={"Surround","Headphones","Stereo"};
  text=modes[sound_];break;
 }
 case Page::Controller:controller_options::value(settings_,i,out,cap);return;
 case Page::Display:{
  constexpr const char* modes[]={"Original","Wide","Stretch"};
  constexpr const char* locks[]={"Off","30","60"};
  text=i==0?(settings_.res3d==Res3d::Native?"Native":"Original"):i==1?modes[settings_.screen_mode%3]:locks[effective_fps_lock(settings_)%3];
  break;
 }
 case Page::Developer:
  if(toggle_row(i))text=toggle_value(i)?"On":"Off";
  else if(i==1)text=settings_.developer_dumps?"L+R+A":"Disabled";
  break;
 }
 std::snprintf(out,cap,"%s",text);
}
void Menu::finish(bool accept){
 accepted_=accept;finished_=true;
 if(accept){if(!(settings_==original_))requests_|=SaveSettings;}
 else{settings_=original_;sound_=original_sound_;}
}
void Menu::back(){
 if(page_==Page::ConfirmDelete){page_=Page::Developer;selected_=2;}
 else if(page_==Page::Options||direct_controller_)finish(false);
 else{page_=Page::Options;selected_=0;}
 footer_=0;
}
void Menu::activate(int d){
 if(selected_==count()){if(footer_==0)finish(true);else back();return;}
 const unsigned i=selected_;
 switch(page_){
 case Page::Options:{
  constexpr Page pages[]={Page::Controller,Page::Audio,Page::Display,Page::Developer,Page::Update};
  page_=pages[i+(general_?0:1)];selected_=0;break;
 }
 case Page::Audio:
  if(i==0)sound_=unsigned((int(sound_)+d+3)%3);
  else settings_.audio_volume=uint8_t(std::max(0,std::min(200,int(settings_.audio_volume)+d*5)));
  break;
 case Page::Controller:controller_options::adjust(settings_,i,d);break;
 case Page::Display:
  if(i==0)settings_.res3d=settings_.res3d==Res3d::Native?Res3d::Ds:Res3d::Native;
  else if(i==1)settings_.screen_mode=uint8_t((int(settings_.screen_mode)+d+3)%3);
  else settings_.fps_lock=uint8_t((int(effective_fps_lock(settings_))+d+3)%3);
  break;
 case Page::Developer:
  if(toggle_row(i))set_toggle(i,!toggle_value(i));
  else if(i==1){if(settings_.developer_dumps)requests_|=DumpFull;}
  else if(i==2){page_=Page::ConfirmDelete;selected_=0;}
  break;
 case Page::ConfirmDelete:
  if(i==1)requests_|=DeleteDumps;
  page_=Page::Developer;selected_=2;break;
 case Page::Update:break;
 }
}
// Audio volume: tap the -/+ ends for 5 % steps or drag along the track.
bool Menu::update_volume_touch(const Input& in,bool& changed){
 using namespace layout;
 const bool touching=in.touch_down||in.touch_pressed;
 const int row_y=row_top+row_pitch;
 if(!(page_==Page::Audio&&touching&&(volume_dragging_||(in.touch_pressed&&in.y>=row_y&&in.y<row_y+row_height&&
    in.x>=value_left-8&&in.x<row_right-10))))return false;
 const bool selection_changed=selected_!=1;
 const auto previous=settings_.audio_volume;
 selected_=1;
 if(!volume_dragging_&&in.x<slider_left){settings_.audio_volume=uint8_t(std::max(0,int(previous)-5));}
 else if(!volume_dragging_&&in.x>slider_right){settings_.audio_volume=uint8_t(std::min(200,int(previous)+5));}
 else{
  volume_dragging_=true;
  const int x=std::max(slider_left,std::min(slider_right,in.x)),span=slider_right-slider_left;
  const int value=((x-slider_left)*200+span/2)/span;
  settings_.audio_volume=uint8_t(std::min(200,((value+2)/5)*5));
 }
 changed=selection_changed||previous!=settings_.audio_volume;
 return true;
}
// Delete confirmation: two horizontal actions and the Back control only.
bool Menu::update_confirm(const Input& in){
 using namespace layout;
 if(in.touch_pressed){
  if(in.y>=confirm_y&&in.y<confirm_y+confirm_height){
   if(in.x>=confirm_left&&in.x<confirm_left+confirm_width){selected_=0;activate(1);return true;}
   if(in.x>=confirm_right&&in.x<confirm_right+confirm_width){selected_=1;activate(1);return true;}
  }
  if(in.x>=back_left&&in.x<back_right&&in.y>=footer_y&&in.y<footer_y+footer_h){back();return true;}
  return false;
 }
 if(in.down&KeyUp)selected_=(selected_+2)%3;
 if(in.down&KeyDown)selected_=(selected_+1)%3;
 if(in.down&KeyLeft)selected_=0;
 if(in.down&KeyRight)selected_=1;
 footer_=1;
 if(in.down&KeyA)activate(1);
 return in.down!=0;
}
bool Menu::update_touch(const Input& in){
 using namespace layout;
 const unsigned n=count();
 // The original blue down arrow advances one page, cycling to the first.
 if(n>unsigned(rows_per_page)&&in.x>=page_down_x&&in.x<page_down_x+page_down_w&&in.y>=page_down_y&&in.y<page_down_y+page_down_h){
  selected_=first()+rows_per_page<n?first()+rows_per_page:0;return true;
 }
 if(in.y>=footer_y&&in.y<footer_y+footer_h&&((in.x>=ok_left&&in.x<ok_right)||(in.x>=back_left&&in.x<back_right))){
  selected_=n;footer_=in.x<back_left?0:1;activate(1);return true;
 }
 if(in.y<row_top||in.x<row_left||in.x>=row_right||(in.y-row_top)%row_pitch>=row_height)return false;
 const unsigned row=unsigned((in.y-row_top)/row_pitch);
 if(row>=unsigned(rows_per_page)||first()+row>=n)return false;
 const unsigned target=first()+row;selected_=target;
 if(toggle_row(target)){
  if(in.x>=value_left){const bool want_on=in.x<value_left+toggle_width+toggle_gap/2;if(toggle_value(target)!=want_on)set_toggle(target,want_on);}
  return true;
 }
 // Developer "Dump RAM": its arrows toggle capture permission; only A or
 // the middle of the value requests a capture.
 if(page_==Page::Developer&&target==1&&((in.x>=arrow_left-2&&in.x<value_left)||in.x>=arrow_right)){settings_.developer_dumps=!settings_.developer_dumps;return true;}
 activate(in.x<value_left-8?-1:1);
 return true;
}
bool Menu::update(Input in){
 if(finished_)return false;
 if(in.down&KeyB){volume_dragging_=false;back();return true;}
 if(!(in.touch_down||in.touch_pressed))volume_dragging_=false;
 bool volume_changed=false;
 if(update_volume_touch(in,volume_changed))return volume_changed;
 if(page_==Page::ConfirmDelete)return update_confirm(in);
 const unsigned n=count();
 if(in.down&KeyUp)selected_=(selected_+n)%(n+1);
 if(in.down&KeyDown)selected_=(selected_+1)%(n+1);
 if(in.touch_pressed)return update_touch(in);
 if(page_==Page::Developer&&selected_==1&&(in.down&(KeyRight|KeyLeft))&&!(in.down&KeyA)){
  settings_.developer_dumps=!settings_.developer_dumps;return true;
 }
 if(selected_==n){if(in.down&(KeyRight|KeyLeft))footer_^=1;if(in.down&KeyA)activate(1);}
 else if(in.down&(KeyA|KeyRight))activate(1);
 else if(in.down&KeyLeft)activate(-1);
 return in.down!=0;
}
}

namespace enhance::native_pause {
bool render_volume_slider(unsigned volume,uint8_t* characters,std::size_t chars_size,uint16_t* map,std::size_t map_entries){
 if(!characters||!map||chars_size<0x6480||map_entries<1024)return false;
 volume=std::min(volume,200u);
 constexpr unsigned w=native_volume_sprite::canvas_width,h=native_volume_sprite::canvas_height;
 uint8_t pixels[w*h]{};
 const auto blit=[&](const uint8_t* source,unsigned sw,unsigned sh,int x,int y){for(unsigned yy=0;yy<sh;++yy)for(unsigned xx=0;xx<sw;++xx){int dx=x+int(xx),dy=y+int(yy);uint8_t c=source[yy*sw+xx];if(c&&dx>=0&&dy>=0&&dx<int(w)&&dy<int(h))pixels[unsigned(dy)*w+unsigned(dx)]=c;}};
 blit(native_volume_sprite::track,native_volume_sprite::track_width,native_volume_sprite::track_height,16,0);
 unsigned end=16+volume*112/200;
 for(unsigned x=17;x<end;++x)blit(native_volume_sprite::fill,1,4,int(x),6);
 blit(native_volume_sprite::minus,7,7,0,4);blit(native_volume_sprite::plus,7,7,137,4);
 blit(native_volume_sprite::handle,9,16,int(end)-4,0);
 constexpr unsigned columns=w/8,tile=native_volume_sprite::tile_base;
 std::memset(characters+tile*32,0,w*h/2);
 for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){unsigned offset=(tile+(y/8)*columns+x/8)*32+(y%8)*4+(x%8)/2,shift=(x%2)*4;characters[offset]=uint8_t((characters[offset]&~(15u<<shift))|(unsigned(pixels[y*w+x])<<shift));}
 for(unsigned y=0;y<h/8;++y)for(unsigned x=0;x<columns;++x)map[(native_volume_sprite::track_y/8+y)*32+native_volume_sprite::canvas_x/8+x]=uint16_t(0x1000+tile+y*columns+x);
 return true;
}
}

namespace enhance::native_pause {
bool render_native_panel(unsigned y,uint8_t* characters,std::size_t chars_size,uint16_t* map,std::size_t map_entries){
 constexpr unsigned w=native_volume_sprite::frame_width,h=native_volume_sprite::frame_height,cols=w/8,tile=native_volume_sprite::frame_tile_base;
 constexpr unsigned end=(tile+cols*h/8)*32;
 static_assert(tile>=0x324&&tile+cols*h/8<=0x400,"panel must not overlap glyphs/slider or exceed DS4bpp tile range");
 if(!characters||!map||chars_size<end||map_entries<1024||y%8||y>192-h)return false;
 std::memset(characters+tile*32,0,w*h/2);
 for(unsigned yy=0;yy<h;++yy)for(unsigned x=0;x<w;++x){unsigned offset=(tile+(yy/8)*cols+x/8)*32+(yy%8)*4+(x%8)/2,shift=(x%2)*4;characters[offset]=uint8_t((characters[offset]&~(15u<<shift))|(unsigned(native_volume_sprite::frame[yy*w+x])<<shift));}
 for(unsigned yy=0;yy<h/8;++yy)for(unsigned x=0;x<cols;++x)map[(y/8+yy)*32+native_volume_sprite::frame_x/8+x]=uint16_t(0x6000+tile+yy*cols+x);
 return true;
}
}

#if defined(SM64DS_PLATFORM_PC) && defined(__3DS__)
#include "native_pause_art.hpp"
#include "native_lower_state.hpp"
#include "ntr/mmio.h"
#include <3ds.h>
extern "C" {
extern uint8_t data_0209f1ec,data_0209f248,data_0209f2e0,data_0209f29c,data_0209f2b4,data_0209f2dc,data_0209f2c4,data_0209f22c;
extern uint16_t data_0209f360[];
extern uint8_t data_0209f2e4,data_0209f244,data_0209f2cc,data_0209f23c;
extern uint8_t data_020a0e40;
extern uint16_t data_020a0e5a;
uint16_t* _ZN3G2S12GetBG1ScrPtrEv();uint16_t* _ZN3G2S12GetBG0ScrPtrEv();uint8_t* _ZN3G2S13GetBG0CharPtrEv();
void LoadCompressedFileAt(int,void*);int GetOwnerLanguage();int GetSoundMode();void SetSoundMode(int);void SetControllerMode(int);
extern uint8_t data_0208f174[],data_0208f074[],data_0209d6a8;
int func_02054d88();
void _ZN3OAM9RenderSubEP7OamAttrii(void*,int,int);extern uint8_t _ZN3OAM9TINY_STARE;
extern uint8_t _ZN3OAM25SMALL_ARROW_POINTING_LEFTE,_ZN3OAM26SMALL_ARROW_POINTING_RIGHTE;
void _ZN5Stage17UpdateMenuButtonsEb(int);
}
namespace {
using namespace enhance::native_pause;
Menu menu;bool active=false,front_active=false,pending_restore=false;unsigned parent_state=0,parent_selection=0;
uint32_t requests=0;VramBackup vram_backup;
void backup_vram(){(void)vram_backup.capture(_ZN3G2S13GetBG0CharPtrEv(),0x10000,_ZN3G2S12GetBG0ScrPtrEv(),1024,_ZN3G2S12GetBG1ScrPtrEv(),1024);}
uint64_t ui_revision=0;
void paint(){
 // UI pixels are owned by a native320x240 image, not retail character VRAM.
 ++ui_revision;
 if(front_active)return;
 data_0209f2e0=uint8_t(menu.selected()<menu.count()?menu.selected()-menu.first():3);
 data_0209f29c=uint8_t(menu.footer());
 const unsigned buttons=menu.settings().controller_mode;
 if(menu.page()==Page::Controller&&data_0209f2dc!=buttons){
  const auto old=uint16_t(ntr::io_read(0x0400000c,2));
  ntr::io_write(0x0400000c,enhance::controller_options::preview_bg2cnt(old,buttons),2);
 }
 data_0209f2dc=uint8_t(buttons);
}
}
extern "C" void sm64ds_native_pause_reset(){active=front_active=false;pending_restore=false;parent_state=parent_selection=0;vram_backup.reset();}
extern "C" void sm64ds_native_pause_parent(unsigned state){if(state==1||state==3||state==5){parent_state=state;parent_selection=data_0209f2e0;backup_vram();}}
extern "C" void sm64ds_native_pause_begin(int controller){backup_vram();menu.begin(enhance::snapshot(),unsigned(GetSoundMode()),controller!=0);active=true;data_0209f2e4=0;data_0209f244=0;data_0209f2cc=0;data_0209f23c=0;paint();}
// Entered by the original visible wrench element's validated selection event.
extern "C" int sm64ds_front_options_begin(){
 if(front_active)return 1;
 if(active)return 0;
 menu.begin_general(enhance::snapshot(),unsigned(GetSoundMode()));
 active=front_active=true;paint();std::puts("Main Options: opened from original wrench");return 1;
}
extern "C" int sm64ds_front_options_active(){return front_active;}
extern "C" void sm64ds_front_options_update(uint32_t down,bool pen_down,bool pen_pressed,int x,int y){
 if(!front_active)return;
 Input input;input.down=down;input.touch_down=pen_down;input.touch_pressed=pen_pressed;input.x=x;input.y=y;
 bool changed=menu.update(input);auto settings=menu.settings();
 if(!(settings==enhance::snapshot()))enhance::publish(settings);
 requests|=menu.take_requests();
 if(menu.finished()){
  if(menu.accepted()){SetControllerMode(settings.controller_mode);SetSoundMode(int(menu.sound()));}
  active=front_active=false;++ui_revision;std::puts("Main Options: returned to original menu");return;
 }
 if(changed)paint();
}
extern "C" void sm64ds_native_pause_update(){if(!active)return;unsigned slot=data_020a0e40;Input input;input.down=*(reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(&data_020a0e5a)+slot*4));
 // Native lower-LCD pixels from HID: the menu art is 320x240 whatever the DS image scale.
 {touchPosition pen{};hidTouchRead(&pen);input.touch_down=(hidKeysHeld()&KEY_TOUCH)!=0;input.touch_pressed=(hidKeysDown()&KEY_TOUCH)!=0;input.x=pen.px;input.y=pen.py;}
 bool changed=menu.update(input);auto settings=menu.settings();if(!(settings==enhance::snapshot()))enhance::publish(settings);requests|=menu.take_requests();if(menu.finished()){if(menu.accepted()){SetControllerMode(settings.controller_mode);SetSoundMode(int(menu.sound()));}(void)vram_backup.restore();active=false;if(parent_state){data_0209f1ec=uint8_t(parent_state-1);pending_restore=true;}else{data_0209f2c4=2;data_0209f22c=0;}return;}if(changed)paint();}
extern "C" int sm64ds_native_pause_render_cursor(){return active?1:0;}
extern "C" void sm64ds_native_pause_restore_parent(){if(pending_restore&&data_0209f1ec==parent_state){data_0209f2e0=uint8_t(parent_selection);_ZN5Stage17UpdateMenuButtonsEb(0);pending_restore=false;parent_state=0;}}
extern "C" void sm64ds_native_pause_controller_label(unsigned){
 // The native root label no longer overwrites temporary retail glyph slots.
}
// Pause root layout. States 1/3/5 show the root; 0/2/4 enter it (and are
// passed when returning from a native submenu). The retail buttons are
// already on screen during entry, so the native root covers 0..5 with the
// last confirmed layout, identical on every pause. States >= 6 are retail
// confirmation/save screens and stay untouched.
namespace {
uint16_t root_positions[4]={0x80,0x120,0x1c0,0x260};
unsigned root_count=4;
bool pause_root_state(unsigned state){return state<=5;}
void refresh_root_layout(){
 const unsigned state=data_0209f248,count=data_0209f2b4;
 if((state==1||state==3||state==5)&&count>0&&count<=4){
  root_count=count;for(unsigned i=0;i<count;++i)root_positions[i]=data_0209f360[i];
 }
}
}
extern "C" uint64_t sm64ds_native_pause_image_key(){
 if(front_active)return (ui_revision<<16)|0xc000u|(unsigned(menu.page())+1u);
 if(!enhance::live().pause_scene.load(std::memory_order_acquire))return 0;
 if(active)return (ui_revision<<16)|0x8000u|(unsigned(menu.page())+1u);
 if(!pause_root_state(data_0209f248))return 0;
 refresh_root_layout();
 uint64_t key=uint64_t(data_0209f2e0)<<40|uint64_t(root_count)<<32;
 for(unsigned i=0;i<root_count;++i)key^=uint64_t(root_positions[i])<<(i*8);
 return key|1u;
}
extern "C" void sm64ds_native_pause_image(uint32_t* pixels){
 std::fill(pixels,pixels+320*240,0u);native_lower::Canvas out{pixels};
 // Front options cover the original face and title buttons; in-game pause
 // keeps its transparent background over the level and map.
 if(front_active)enhance::render_menu_background(pixels);
 if(active)native_lower::menu(out,menu);
 else native_lower::root(out,data_0209f2e0,root_count,root_positions);
}
extern "C" uint32_t sm64ds_native_pause_take_requests(){uint32_t result=requests;requests=0;return result;}
#endif
