#include "native_pause_menu.h"
#include "controller_preview.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <array>
#include <limits>
using namespace enhance;using namespace enhance::native_pause;
void key(Menu& m,unsigned k){m.update({k,false,0,0});}
// Fixtures were authored in DS touch units; the menu now receives native
// lower-LCD pixels. Same 5/4 mapping the previous model applied internally.
Input ds(Input in){in.x=in.x*5/4;in.y=in.y*5/4;return in;}
int main(){
 for(unsigned mode=0;mode<3;++mode)for(unsigned flags=0;flags<65536;++flags){
  const unsigned preview=controller_options::preview_bg2cnt(uint16_t(flags),mode);
  assert((preview>>8)==0xdu+mode);assert((preview&0xff)==((flags&0x43)|0x10));
 }
 Settings initial=Settings::safe();assert(initial.res3d==Res3d::Native&&initial.screen_mode==1);assert(initial.developer_dumps);initial.developer_dumps=false; // fixtures start with dumps off
 initial.res3d=Res3d::Ds;initial.screen_mode=0;Menu m;char value[64];uint8_t e[8];assert(encode("Ab 09!",e,8)==6);const uint8_t expected[]={0xa,0x2e,0x4d,0,9,0x27,0xff};assert(!std::memcmp(e,expected,7));uint8_t guarded[]={0x11,0,0,0,0x22};assert(encode("too long",guarded+1,3)==2);assert(guarded[0]==0x11&&guarded[4]==0x22&&guarded[3]==0xff);
 m.begin(initial,2,true);assert(m.count()==controller_options::count);assert(!std::strcmp(m.label(0),"Buttons"));assert(!std::strcmp(m.label(1),"Control"));assert(!std::strcmp(m.label(2),"C-Stick Sensitivity"));assert(!std::strcmp(m.label(3),"Circle Pad Sensitivity"));assert(!std::strcmp(m.label(4),"Invert X Axis"));assert(!std::strcmp(m.label(5),"Invert Y Axis"));
 // Arrows change Buttons/Control in place: no submenu.
 key(m,16);assert(m.page()==Page::Controller&&m.settings().controller_mode==1);key(m,32);key(m,32);assert(m.settings().controller_mode==2);key(m,16);assert(m.settings().controller_mode==0);key(m,16);
 key(m,128);key(m,16);assert(m.settings().physics_mode==1&&m.settings().controller_mode==1);key(m,16);assert(m.settings().physics_mode==2);
 key(m,128);key(m,16);assert(m.settings().camera_sensitivity==105&&m.settings().camera_pitch_sensitivity==105&&m.settings().move_sensitivity==100);key(m,128);key(m,32);assert(m.settings().move_sensitivity==95);
 key(m,128);assert(m.toggle_row(4)&&!m.toggle_row(3));m.value(4,value,sizeof value);assert(!std::strcmp(value,"Off"));key(m,1);assert(m.settings().camera_invert_x&&!m.settings().camera_invert_y);
 key(m,128);key(m,16);assert(m.settings().camera_invert_y);m.value(5,value,sizeof value);assert(!std::strcmp(value,"On"));
 key(m,128);key(m,1);assert(m.finished()&&m.accepted());assert(m.take_requests()==SaveSettings);assert(!m.take_requests());
 m.begin(initial,2,true);key(m,16);key(m,128);key(m,1);key(m,2);key(m,2);assert(m.finished()&&!m.accepted()&&m.settings()==initial&&!m.take_requests());
 m.begin(initial,0,false);m.value(0,value,sizeof value);assert(!value[0]);assert(!std::strcmp(m.label(0),"Audio"));key(m,1);assert(m.page()==Page::Audio&&m.count()==2);assert(!std::strcmp(m.label(0),"Sound mode"));m.value(0,value,sizeof value);assert(!std::strcmp(value,"Surround"));key(m,16);assert(m.sound()==1);m.value(0,value,sizeof value);assert(!std::strcmp(value,"Headphones"));key(m,16);assert(m.sound()==2);m.value(0,value,sizeof value);assert(!std::strcmp(value,"Stereo"));key(m,32);assert(m.sound()==1);key(m,2);assert(m.page()==Page::Options&&!m.finished());key(m,128);key(m,1);assert(m.page()==Page::Display);key(m,16);assert(m.settings().res3d==Res3d::Native);key(m,128);key(m,16);assert(m.settings().screen_mode==1);key(m,2);assert(m.page()==Page::Options);key(m,128);key(m,128);key(m,1);assert(m.page()==Page::Developer);assert(!std::strcmp(m.label(1),"Dump RAM"));m.value(2,value,sizeof value);assert(!value[0]);key(m,128);key(m,1);assert(!m.take_requests());key(m,64);key(m,1);assert(m.settings().developer_dumps);key(m,128);key(m,1);assert(m.take_requests()==DumpFull);key(m,128);key(m,1);assert(m.page()==Page::ConfirmDelete&&m.selected()==0);key(m,1);assert(m.page()==Page::Developer&&!m.take_requests());key(m,1);key(m,128);key(m,1);assert(m.take_requests()==DeleteDumps);key(m,128);key(m,1);assert(m.settings().show_fps);key(m,128);assert(m.first()==3);key(m,1);assert(m.settings().developer_overlay);key(m,2);assert(m.page()==Page::Options);key(m,128);key(m,128);key(m,128);key(m,1);assert(m.page()==Page::Update&&m.count()==0&&m.first()==0);key(m,2);assert(m.page()==Page::Options);key(m,2);assert(m.finished()&&!m.accepted()&&m.settings()==initial);
 for(unsigned mode=0;mode<3;++mode){
  auto settings=initial;settings.controller_mode=uint8_t(mode);
  m.begin(settings,2,true);m.value(0,value,sizeof value);const char* modes[]={"Standard","Touch","Dual-hand"};assert(!std::strcmp(value,modes[mode]));
  const unsigned target=(mode+1)%3;m.update(ds({0,true,140,44}));   // right half of the Buttons value
  assert(m.page()==Page::Controller&&m.settings().controller_mode==target&&m.settings().camera_sensitivity==settings.camera_sensitivity&&m.settings().move_sensitivity==settings.move_sensitivity);
  m.value(0,value,sizeof value);assert(!std::strcmp(value,modes[target]));
  key(m,2);assert(m.finished()&&!m.accepted()&&m.settings()==settings);
 }
 // Invert toggles take the touched ON/OFF state, like Developer toggles.
 m.begin(initial,2,true);m.update(ds({0,true,128,144}));m.update(ds({0,true,140,88}));assert(m.first()==3&&m.selected()==4&&m.settings().camera_invert_x);
 m.update(ds({0,true,140,88}));assert(m.settings().camera_invert_x);m.update(ds({0,true,190,88}));assert(!m.settings().camera_invert_x);
 // Touch selects original rows and uses the actual small right-hand Back area.
 m.begin(initial,2,true);m.update(ds({0,true,110,120}));assert(m.settings().camera_sensitivity==95);m.update(ds({0,true,230,168}));assert(m.finished()&&!m.accepted());
 m.begin(initial,2,true);m.update(ds({0,true,128,144}));assert(m.first()==3);m.update(ds({0,true,140,48}));assert(m.settings().move_sensitivity==105);m.update(ds({0,true,128,168}));assert(m.finished()&&m.accepted()&&m.take_requests()==SaveSettings);
 // Every category is reachable with touch alone. Arrow taps never activate
 // the row beneath them and unavailable arrows are inert.
 m.begin(initial,2,false);const Settings before_page=m.settings();
 m.update(ds({0,true,128,144}));assert(m.first()==3&&m.selected()==3&&m.settings()==before_page);
 m.update(ds({0,true,140,48}));assert(m.page()==Page::Update&&m.count()==0);
 m.update(ds({0,true,128,144}));assert(m.page()==Page::Update&&!m.finished());
 m.update(ds({0,true,230,168}));assert(m.page()==Page::Options&&!m.finished());
 m.update(ds({0,true,140,124}));assert(m.page()==Page::Developer);
 m.update(ds({0,true,24,144}));assert(m.selected()==0&&!m.settings().developer_dumps);
 m.update(ds({0,true,128,144}));assert(m.first()==3&&!m.settings().show_fps&&!m.settings().developer_overlay);
 m.update(ds({0,true,140,48}));assert(m.settings().show_fps);
 m.update(ds({0,true,140,88}));assert(m.settings().developer_overlay);
 const Settings before_prev=m.settings();m.update(ds({0,true,128,144}));assert(m.first()==0&&m.selected()==0&&m.settings()==before_prev);
 m.update(ds({0,true,140,60}));assert(m.selected()==0&&m.settings()==before_prev); // inter-row gap
 // Audio also opens/returns using only the original touch row/Back.
 m.begin(initial,0,false);m.update(ds({0,true,140,48}));assert(m.page()==Page::Audio&&m.count()==2);m.update(ds({0,true,140,48}));assert(m.sound()==1);m.update(ds({0,true,230,168}));assert(m.page()==Page::Options&&!m.finished());key(m,2);assert(m.finished()&&!m.accepted()&&m.sound()==0);
 // Volume has a visible percent, +/-5 controls and genuine held-touch drag.
 m.begin(initial,0,false);key(m,1);assert(m.count()==2&&!std::strcmp(m.label(1),"Volume"));key(m,128);m.value(1,value,sizeof value);assert(!std::strcmp(value,"100%"));
 key(m,16);assert(m.settings().audio_volume==105);key(m,32);assert(m.settings().audio_volume==100);
 for(int i=0;i<100;++i)key(m,16);assert(m.settings().audio_volume==200);for(int i=0;i<100;++i)key(m,32);assert(m.settings().audio_volume==0);
 m.update(ds({0,true,161,90,true}));assert(m.settings().audio_volume==100);m.update(ds({0,false,189,90,true}));assert(m.settings().audio_volume==200);m.update(ds({0,false,1,50,true}));assert(m.settings().audio_volume==0);m.update(ds({0,false,255,180,true}));assert(m.settings().audio_volume==200);m.update(ds({0,false,161,90,true}));assert(m.settings().audio_volume==100);m.update(ds({0,false,161,90,false}));
 m.update(ds({0,true,232,90,true}));assert(m.settings().audio_volume==105);m.update(ds({0,false,232,90,true}));assert(m.settings().audio_volume==105);m.update(ds({0,false,232,90,false}));m.update(ds({0,true,120,90,true}));assert(m.settings().audio_volume==100);key(m,128);key(m,1);assert(m.finished()&&m.accepted());
 m.begin(initial,0,false);key(m,1);m.update(ds({0,true,189,90,true}));assert(m.settings().audio_volume==200);m.update(ds({2,false,189,90,true}));assert(m.page()==Page::Options);key(m,2);assert(m.finished()&&!m.accepted()&&m.settings().audio_volume==100);
 uint8_t percentage[8];assert(encode("100%",percentage,sizeof percentage)==4&&percentage[3]==0x4f&&percentage[4]==0xff);
 // Stationary drags and saturated side taps do not repaint identical UI.
 m.begin(initial,0,false);key(m,1);
 assert(m.update(ds({0,true,161,90,true})));
 for(unsigned i=0;i<120;++i)assert(!m.update(ds({0,false,161,90,true})));
 assert(m.update(ds({0,false,189,90,true})));
 assert(!m.update(ds({0,false,189,90,true})));
 m.update(ds({0,false,189,90,false}));
 assert(!m.update(ds({0,true,232,90,true})));
 // Exercise the actual DS4bpp renderer with guarded VRAM/map buffers.
 std::array<uint8_t,0x10000+2> chars;std::array<uint16_t,1024+2> map;
 for(unsigned volume:{0u,100u,200u,255u}){chars.fill(0xa5);map.fill(0xbeef);assert(render_volume_slider(volume,chars.data()+1,0x10000,map.data()+1,1024));assert(chars.front()==0xa5&&chars.back()==0xa5&&map.front()==0xbeef&&map.back()==0xbeef);
  for(unsigned i=0;i<0x10000;++i)if(i<0x6000||i>=0x6480)assert(chars[i+1]==0xa5);
  for(unsigned i=0;i<1024;++i){bool cell=(i/32==13||i/32==14)&&i%32>=7&&i%32<25;if(cell)assert(map[i+1]>=0x1300&&map[i+1]<=0x1323);else assert(map[i+1]==0xbeef);}
 }
 chars.fill(0xa5);map.fill(0xbeef);assert(!render_volume_slider(100,chars.data()+1,0x647f,map.data()+1,1024));assert(chars[0x6001]==0xa5&&map[360]==0xbeef);
 for(unsigned y:{56u,72u}){chars.fill(0xa5);map.fill(0xbeef);assert(render_native_panel(y,chars.data()+1,0x10000,map.data()+1,1024));assert(chars.front()==0xa5&&chars.back()==0xa5&&map.front()==0xbeef&&map.back()==0xbeef);
  for(unsigned i=0;i<0x10000;++i)if(i<0x6800||i>=0x7d00)assert(chars[i+1]==0xa5);
  for(unsigned i=0;i<1024;++i){bool cell=i/32>=y/8&&i/32<y/8+7&&i%32>=4&&i%32<28;if(cell)assert(map[i+1]>=0x6340&&map[i+1]<=0x63e7);else assert(map[i+1]==0xbeef);}
 }
 chars.fill(0xa5);map.fill(0xbeef);assert(!render_native_panel(std::numeric_limits<unsigned>::max()-7,chars.data()+1,0x10000,map.data()+1,1024));assert(!render_native_panel(73,chars.data()+1,0x10000,map.data()+1,1024));assert(!render_native_panel(144,chars.data()+1,0x10000,map.data()+1,1024));assert(!render_native_panel(72,chars.data()+1,0x7cff,map.data()+1,1024));assert(chars[0x6801]==0xa5&&map[320]==0xbeef);
 // The expanded Volume row accepts its padded area; label/slider never overlap.
 m.begin(initial,0,false);key(m,1);m.update(ds({0,true,232,90}));assert(m.selected()==1&&m.settings().audio_volume==105);m.update(ds({0,true,120,90}));assert(m.settings().audio_volume==100);
 // Native temporary glyph/frame tiles may overlap the retail menu. Restore
 // exact original character bytes/maps on every exit, before parent repaint.
 VramBackup backup;
 for(unsigned page=0;page<3;++page)for(bool accept:{false,true}){
  for(unsigned i=0;i<0x10000;++i)chars[i+1]=uint8_t(i*37+page);for(unsigned i=0;i<1024;++i)map[i+1]=uint16_t(i*13+page);
  auto original_chars=chars;auto original_map=map;std::array<uint16_t,1024> bg1;for(unsigned i=0;i<1024;++i)bg1[i]=uint16_t(i*17+0x5000);auto original_bg1=bg1;
  assert(backup.capture(chars.data()+1,0x10000,map.data()+1,1024,bg1.data(),1024));
  m.begin(initial,0,page==1);if(page==0)key(m,1);if(page==2){key(m,128);key(m,128);key(m,128);key(m,1);}
  std::memset(chars.data()+1+0x4000,0,0x2000); // actual native glyph clearing
  if(page==0){assert(render_native_panel(72,chars.data()+1,0x10000,bg1.data(),1024));assert(render_volume_slider(200,chars.data()+1,0x10000,map.data()+1,1024));}
  if(page==2)assert(render_native_panel(56,chars.data()+1,0x10000,bg1.data(),1024));
  assert(backup.capture(chars.data()+1,0x10000,map.data()+1,1024,bg1.data(),1024)); // begin cannot overwrite parent snapshot
  if(accept){for(unsigned i=0;i<m.count();++i)key(m,128);key(m,1);}else{key(m,2);if(!m.finished())key(m,2);}
  assert(m.finished()&&m.accepted()==accept);assert(backup.restore());assert(!backup.saved());assert(chars==original_chars&&map==original_map&&bg1==original_bg1);
  chars[0x4ff1]=0x43;assert(!backup.restore()&&chars[0x4ff1]==0x43); // later parent CONTROLLER label survives
 }
 // Real maps can alias their character bank; restoring them is still exact.
 alignas(4) std::array<uint8_t,0x10000> aliased;for(unsigned i=0;i<aliased.size();++i)aliased[i]=uint8_t(i*11);auto original_alias=aliased;
 assert(backup.capture(aliased.data(),aliased.size(),reinterpret_cast<uint16_t*>(aliased.data()+0xc000),1024,reinterpret_cast<uint16_t*>(aliased.data()+0xd000),1024));aliased.fill(0);assert(backup.restore()&&aliased==original_alias);
 assert(!backup.capture(aliased.data(),0xffff,map.data()+1,1024,map.data()+1,1024));assert(!backup.saved());
 // Closed bounds hold across repeated directional changes.
 m.begin(initial,2,true);key(m,128);key(m,128);for(int i=0;i<500;++i)key(m,16);assert(m.settings().camera_sensitivity==200);for(int i=0;i<500;++i)key(m,32);assert(m.settings().camera_sensitivity==25);key(m,128);for(int i=0;i<500;++i)key(m,16);assert(m.settings().move_sensitivity==200);m.value(3,value,sizeof value);assert(!std::strcmp(value,"200"));// Front-menu options expose the same controller model and transactional settings.
 m.begin_general(initial,0);assert(m.count()==5);assert(!std::strcmp(m.label(0),"Controller"));
 key(m,1);assert(m.page()==Page::Controller);key(m,1);assert(m.page()==Page::Controller&&m.settings().controller_mode==1);key(m,2);
 assert(m.page()==Page::Options&&!m.finished());key(m,2);
 assert(m.finished()&&!m.accepted()&&m.settings()==initial);
 m.begin_general(initial,0);for(unsigned i=0;i<5;++i)key(m,128);key(m,1);
 assert(m.finished()&&m.accepted()&&!m.take_requests());
 m.begin_general(initial,0);key(m,1);key(m,1);key(m,2);for(unsigned i=0;i<5;++i)key(m,128);key(m,1);assert(m.finished()&&m.accepted()&&m.take_requests()==SaveSettings);
 // Full orange submenu rows open the category from the left half too.
 for(unsigned category=0;category<5;++category){
  m.begin_general(initial,0);
  if(category>=3)m.update(ds({0,true,128,144}));
  m.update(ds({0,true,40,44+int(category%3)*35}));
  const Page expected[]={Page::Controller,Page::Audio,Page::Display,Page::Developer,Page::Update};
  assert(m.page()==expected[category]);
 }
 // Clean only opens the question. The two horizontal hit boxes are distinct;
 // gaps, the retired vertical rows and the old OK position are inert.
 // Dump RAM arrows toggle permission without starting a capture. The centre
 // and A retain the explicit capture action; Full Dumps reflects the same flag.
 m.begin(initial,0,false);key(m,128);key(m,128);key(m,1);key(m,128);
 key(m,16);assert(m.settings().developer_dumps&&!m.take_requests());
 m.value(1,value,sizeof value);assert(!std::strcmp(value,"L+R+A"));
 m.value(0,value,sizeof value);assert(!std::strcmp(value,"On"));
 key(m,32);assert(!m.settings().developer_dumps&&!m.take_requests());
 key(m,1);assert(!m.take_requests());
 m.update(ds({0,true,242,78,true}));assert(m.settings().developer_dumps&&!m.take_requests());
 m.update(ds({0,false,242,78,true}));assert(m.settings().developer_dumps&&!m.take_requests());
 m.update(ds({0,false,242,78,false}));
 m.update(ds({0,true,180,78}));assert(m.take_requests()==DumpFull);
 m.update(ds({0,true,117,78}));assert(!m.settings().developer_dumps&&!m.take_requests());
 m.value(1,value,sizeof value);assert(!std::strcmp(value,"Disabled"));
 m.update(ds({0,true,180,78}));assert(!m.take_requests());
 key(m,16);key(m,1);assert(m.take_requests()==DumpFull);assert(!m.take_requests());
 const auto confirm=[&](){
  m.begin(initial,0,false);key(m,128);key(m,128);key(m,1);
  assert(!std::strcmp(m.label(2),"Clean Dumps"));
  m.update(ds({0,true,180,112}));assert(m.page()==Page::ConfirmDelete&&!m.take_requests());
 };
 for(const auto point:std::array<std::array<int,2>,4>{{{{128,88}},{{140,44}},{{140,124}},{{128,168}}}}){
  confirm();assert(!m.update(ds({0,true,point[0],point[1]})));
  assert(m.page()==Page::ConfirmDelete&&!m.take_requests()&&!m.finished());
 }
 confirm();m.update(ds({0,true,60,88}));assert(m.page()==Page::Developer&&!m.take_requests());
 confirm();m.update(ds({0,true,180,88}));assert(m.page()==Page::Developer&&m.take_requests()==DeleteDumps);assert(!m.take_requests());
 confirm();m.update(ds({0,true,232,168}));assert(m.page()==Page::Developer&&!m.take_requests());
 confirm();key(m,16);assert(m.page()==Page::ConfirmDelete&&m.selected()==1&&!m.take_requests());
 key(m,32);assert(m.selected()==0);key(m,1);assert(m.page()==Page::Developer&&!m.take_requests());
 confirm();key(m,16);key(m,1);assert(m.page()==Page::Developer&&m.take_requests()==DeleteDumps);
 confirm();key(m,128);key(m,128);key(m,1);assert(m.page()==Page::Developer&&!m.finished()&&!m.take_requests());
 // Display > Lock FPS: third row, Auto shows the console default and both
 // directions cycle Off/30/60 explicitly; B rolls the session back.
 {
  Settings lock=Settings::safe();set_console_model(false);
  m.begin_general(lock,0);key(m,128);key(m,128);key(m,1);assert(m.page()==Page::Display&&m.count()==3);
  assert(!std::strcmp(m.label(2),"Lock FPS"));
  key(m,128);key(m,128);assert(m.selected()==2);m.value(2,value,sizeof value);assert(!std::strcmp(value,"30"));
  key(m,16);assert(m.settings().fps_lock==kFpsLock60);m.value(2,value,sizeof value);assert(!std::strcmp(value,"60"));
  key(m,16);assert(m.settings().fps_lock==kFpsLockOff);m.value(2,value,sizeof value);assert(!std::strcmp(value,"Off"));
  key(m,32);assert(m.settings().fps_lock==kFpsLock60);key(m,32);key(m,32);assert(m.settings().fps_lock==kFpsLockOff);
  m.update(ds({0,true,200,int((36+88+18)*4/5)}));assert(m.settings().fps_lock==kFpsLock30);
  key(m,2);key(m,2);assert(m.finished()&&!m.accepted()&&m.settings()==lock);
  set_console_model(true);m.begin_general(lock,0);key(m,128);key(m,128);key(m,1);m.value(2,value,sizeof value);assert(!std::strcmp(value,"60"));
  set_console_model(false);
 }
 std::puts("Native pause model: categories, horizontal confirmation, controls, rollback, actions, Lock FPS and touch bounds passed");}
