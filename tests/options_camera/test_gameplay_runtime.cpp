// Includes the production runtime so publication and input ordering are tested
// together. The runner supplies only OS/UI-service stubs, never camera logic.
#include "enhance_runtime.cpp"
#include <cassert>
#include <cstdio>

static unsigned scene=3;
static bool front=false,hint=false;
static uint64_t pause_key=0;
extern "C" {
alignas(8) unsigned char data_0209f498[4*0x18]{};
unsigned char data_0209f2d8=1,data_0209d660=0,data_0209f21c=1,data_020a0e40=0;
unsigned char data_0209f294=0,data_0209f2c4=0,data_0209f20c=0,data_ov002_02111180=0;
int data_0209caa0[4]{},data_0209fc48=0;
unsigned data_0209b454=0;
alignas(8) unsigned char camera[0x200]{};
void* data_0209f318=camera;
bool sm64ds_intro_skip_input(bool,bool){return false;}
bool sm64ds_intro_skip_hint(){return hint;}
unsigned sm64ds_render60_scene(){return scene;}
int sm64ds_front_options_active(){return front;}
void sm64ds_front_options_update(uint32_t,bool,bool,int,int){}
uint64_t sm64ds_native_pause_image_key(){return pause_key;}
void sm64ds_native_pause_image(uint32_t* p){std::fill_n(p,320*240,0x12345678u);}
}
namespace enhance {
void render_diagnostics(uint32_t* p,const Diagnostics&){std::fill_n(p,320*240,0x87654321u);}
int draw_game_text(uint32_t*,int,int,int,int,const char*,bool){return 0;}
}
static bool poll(bool pen,int x=20,int y=210){
 sm64ds_camera_touch_inject(1,0,pen,x,y);
 bool allowed=true;enhance_runtime::filter_input(pen?KEY_TOUCH:0,0,allowed);return allowed;
}
static uint32_t consume(){
 uint32_t generation=0;int w=0,h=0;
 const auto* pixels=enhance_runtime::overlay_image(0,generation,w,h);assert(pixels&&w==320&&h==240);
 enhance_runtime::acknowledge(0,generation);native_lower::gpu_ready=true;return generation;
}
int main(){
 using namespace enhance_runtime;
 unsigned flags=0x1000;std::memcpy(camera+0x154,&flags,4);
 enhance::publish(enhance::Settings::safe());cstick_camera::set_mode(cstick_camera::Mode::Off);
 poll(false);assert(g_pause_image_key==native_gameplay_camera::image_key&&!native_lower::wanted);
 auto& channel=g_channels[0];const auto next=channel.next;
 assert(poll(true)); // not uploaded: no hidden action
 consume();assert(poll(true)); // contact already belongs to retail
 assert(poll(false));assert(!poll(true));
 std::memset(data_0209f498+4,0,4);cstick_camera::after_check_input();assert(data_0209f498[5]==2);
 assert(channel.next==next); // idle/held arrows reuse the same image
 poll(false);
 // A pending read lease must survive hiding when a modal takes ownership.
 channel.publish();uint32_t generation;int w,h;
 assert(overlay_image(0,generation,w,h));const auto leased_slot=channel.slot.load();
 pause_key=1;data_0209f2c4=1;enhance::live().pause_scene=true;
 poll(false);assert(!channel.visible&&!channel.can_write()&&!native_lower::wanted);
 assert(channel.slot==leased_slot&&channel.published==generation);
 acknowledge(0,generation);poll(false);assert(channel.visible&&native_lower::wanted&&g_pause_image_key==1);
 consume();
 // Resume while a new pause generation is in flight: hide until retired.
 channel.publish();assert(overlay_image(0,generation,w,h));
 pause_key=0;data_0209f2c4=0;enhance::live().pause_scene=false;
 poll(false);assert(!channel.visible&&!native_lower::wanted&&!channel.can_write());
 assert(poll(true));std::memset(data_0209f498+4,0,4);cstick_camera::after_check_input();assert(!data_0209f498[5]);
 acknowledge(0,generation);poll(false);consume();poll(false);assert(!poll(true));poll(false);
 for(unsigned test=0;test<6;++test){
  switch(test){case 0:data_0209f294=1;break;case 1:data_0209fc48=1;break;case 2:data_0209b454=0x40000000;break;case 3:scene=2;break;case 4:front=true;break;case 5:enhance::live().pause_scene=true;break;}
  assert(poll(true)==(test!=4));assert(!channel.visible&&!native_lower::wanted);
  std::memset(data_0209f498+4,0,4);cstick_camera::after_check_input();assert(!data_0209f498[5]);
  data_0209f294=0;data_0209fc48=0;data_0209b454=0;scene=3;front=false;enhance::live().pause_scene=false;
  poll(false);consume();poll(false);
 }
 hint=true;poll(false);assert(g_pause_image_key==~uint64_t(0)&&!native_lower::wanted);consume();
 hint=false;poll(false);consume();
 // Diagnostics reuse the same channel but cannot retain a camera content key
 // on close, even with its own generation still leased by the worker.
 enhance::live().developer_overlay=true;
 bool allowed=true;filter_input(0,KEY_SELECT,allowed);assert(g_diagnostics_open);
 assert(overlay_image(0,generation,w,h));
 filter_input(0,KEY_SELECT,allowed);assert(!g_diagnostics_open&&!channel.visible&&!channel.can_write());
 assert(poll(true));assert(!native_lower::wanted);
 acknowledge(0,generation);poll(false);consume();poll(false);assert(!poll(true));poll(false);
 shutdown();assert(!channel.visible&&!channel.buffer[0]&&!channel.buffer[1]);
 std::puts("PASS: real runtime filter/overlay; upload-gated input; modal lease transitions; unchanged static image; original HUD stays enabled");
}
