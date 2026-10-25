#include "native_gameplay_camera.hpp"
#include "cstick_camera.h"
#include "enhance_settings.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>

extern "C" {
alignas(8) unsigned char data_0209f498[4*0x18]{};
unsigned char data_0209f2d8=1,data_0209d660=0,data_0209f21c=1,data_020a0e40=0;
unsigned char data_0209f294=0,data_0209f2c4=0,data_0209f20c=0,data_ov002_02111180=0;
int data_0209caa0[4]{},data_0209fc48=0;
unsigned data_0209b454=0;
alignas(8) unsigned char camera[0x200]{};
void* data_0209f318=camera;
}
using namespace native_gameplay_camera;
static void flags(unsigned f){std::memcpy(camera+0x154,&f,4);}
static unsigned rd(unsigned n){unsigned short v;std::memcpy(&v,data_0209f498+n,2);return v;}
static void update(){
 std::memset(data_0209f498+4,0,4);cstick_camera::after_check_input();
 assert(!(rd(4)&0xc000)&&!(rd(6)&0xc000));assert(!data_ov002_02111180);
}
static bool touch(bool old,bool down,int x,int y,bool gameplay=true,bool visible=true){
 sm64ds_camera_touch_inject(1,old,down,x,y);return cstick_camera::capture_stylus(gameplay,visible);
}
static void release(){touch(false,false,0,0);update();assert(!rd(4)&&!rd(6));}
int main(int argc,char** argv){
 assert(argc==2);flags(0x1000);sm64ds_cstick_inject(1,0,0);
 std::array<uint32_t,width*height> pixels;pixels.fill(0xdeadbeef);render(pixels.data());
 std::ofstream raw(argv[1],std::ios::binary);raw.write(reinterpret_cast<const char*>(pixels.data()),sizeof pixels);raw.close();
 unsigned area=0,opaque=0;
 for(int y=0;y<height;++y)for(int x=0;x<width;++x){
  const unsigned expected=(x>=10&&x<42&&y>=198&&y<230)?left:(x>=278&&x<310&&y>=198&&y<230)?right:0;
  assert(hit(x,y)==expected);area+=hit(x,y)!=0;opaque+=(pixels[y*width+x]>>24)!=0;
  if(!expected)assert(pixels[y*width+x]==0);
 }
 assert(area==2048&&opaque>0&&opaque<area);
 for(int x:{-1,320,999})for(int y:{-1,0,198,239,240,999})assert(!hit(x,y));

 Touch router;int dx=0,dy=0;
 assert(router.sample(true,true,true,true,20,210));assert(router.buttons()==left&&router.take_pressed()==left);
 assert(router.sample(true,true,true,true,21,210));assert(router.buttons()==left&&!router.take_pressed());
 assert(router.sample(true,true,true,true,100,100));assert(!router.buttons());router.consume_stroke(dx,dy);assert(!dx&&!dy);
 assert(router.sample(true,true,true,true,290,210));assert(!router.buttons()); // no sliding to another button
 router.sample(true,true,true,false,0,0);
 assert(router.sample(true,true,true,true,100,100));router.consume_stroke(dx,dy);assert(!dx&&!dy);
 router.sample(true,true,true,true,110,90);router.consume_stroke(dx,dy);assert(dx==10&&dy==10);
 router.sample(true,true,true,true,20,210);router.consume_stroke(dx,dy);assert(!dx&&!dy&&!router.buttons());
 router.sample(true,true,true,true,110,90);router.consume_stroke(dx,dy);assert(!dx&&!dy);
 router.sample(true,true,true,true,120,80);router.consume_stroke(dx,dy);assert(dx==10&&dy==10);
 router.sample(false,false,true,true,120,80);assert(!router.buttons());
 router.sample(true,true,true,true,20,210);assert(!router.buttons());
 router.sample(true,true,true,false,0,0);
 assert(!router.sample(false,false,false,true,20,210));
 assert(!router.sample(true,true,false,true,20,210)&&!router.buttons()); // no enable under an existing press
 router.sample(true,true,false,false,0,0);
 assert(!router.sample(true,false,false,true,20,210)&&!router.buttons()); // no hidden hitbox
 router.sample(true,true,false,true,20,210);assert(!router.buttons());

 unsigned checks=0;
 for(bool old:{false,true})for(unsigned ctl:{0u,1u,2u})for(unsigned mode:{0u,1u,2u})for(unsigned dir:{left,right}){
  release();data_0209f498[0x16]=ctl;cstick_camera::set_mode(cstick_camera::Mode(mode));
  const int x=dir==left?20:290;
  assert(touch(old,true,x,210));update();assert(rd(4)==dir&&rd(6)==dir);
  short yaw=77;assert(!sm64ds_cstick_cam_follow(&yaw,camera)&&yaw==77);
  assert(sm64ds_cstick_cam_step(0x400)==0x400); // original follow step
  assert(touch(old,true,x,210));update();assert(rd(4)==dir&&!rd(6));
  update();assert(rd(4)==dir&&!rd(6)); // game cadence can exceed HID cadence
  assert(touch(old,true,160,100));update();assert(!rd(4)&&!rd(6));
  assert(touch(old,true,x,210));update();assert(rd(4)==dir&&rd(6)==dir);
  release();++checks;
 }
 // All real draw/input gates are rechecked after capture as well as before it.
 cstick_camera::set_mode(cstick_camera::Mode::Off);data_0209f498[0x16]=0;
 auto gate=[&](){update();assert(!rd(4)&&!rd(6));assert(!cstick_camera::gameplay_camera_available(true));++checks;};
 for(auto* value:{&data_0209f294,&data_0209f2c4,&data_0209f20c,&data_0209d660}){
  release();assert(touch(false,true,20,210));*value=1;gate();
  touch(false,true,20,210);*value=0;touch(false,true,20,210);update();assert(!rd(4));release();
 }
 release();touch(false,true,20,210);data_0209fc48=1;gate();data_0209fc48=0;release();
 touch(false,true,20,210);data_0209b454=0x40000000;gate();data_0209b454=0;release();
 touch(false,true,20,210);flags(0);gate();flags(0x1000);release();
 touch(false,true,20,210);data_0209f2d8=2;gate();data_0209f2d8=1;release();
 touch(false,true,20,210);data_020a0e40=4;gate();data_020a0e40=0;release();
 touch(false,true,20,210);data_0209f21c=0;gate();data_0209f21c=1;release();
 touch(false,true,20,210);data_0209f498[0x16]=3;gate();data_0209f498[0x16]=0;release();
 touch(false,true,20,210);data_0209f318=nullptr;gate();data_0209f318=camera;release();
 touch(false,true,20,210);enhance::live().overlay_open=true;gate();enhance::live().overlay_open=false;release();
 assert(!touch(false,true,20,210,false));update();assert(!rd(4)&&!rd(6));release();
 assert(!touch(false,true,20,210,true,false));update();assert(!rd(4)&&!rd(6));release();
 // The actual Old3DS hook receives deltas outside buttons, no DS key remap.
 enhance::live().camera_sensitivity=100;
 assert(touch(true,true,100,100));update();short yaw=0;sm64ds_cstick_cam_follow(&yaw,camera);assert(!yaw);
 assert(touch(true,true,110,90));update();assert(!rd(4)&&!rd(6));
 assert(sm64ds_cstick_cam_follow(&yaw,camera)&&yaw==1280);
 int vertical=0,horizontal=1000000;sm64ds_cstick_cam_orbit(1000000,&vertical,&horizontal,camera);assert(vertical>0&&horizontal<1000000);
 touch(true,true,20,210);update();const auto before=yaw;sm64ds_cstick_cam_follow(&yaw,camera);assert(yaw==before&&!rd(4));
 touch(true,true,200,100);update();sm64ds_cstick_cam_follow(&yaw,camera);assert(yaw==before);release();
 touch(true,true,250,50);update();sm64ds_cstick_cam_follow(&yaw,camera);assert(yaw==before);release();
 // A talk/script gate can change between HID sampling and CheckInput. No
 // pending stroke or previously engaged orbit may move the camera then.
 touch(true,true,100,100);update();touch(true,true,120,90);
 data_0209b454=0x40000000;update();assert(!sm64ds_cstick_cam_follow(&yaw,camera));
 data_0209b454=0;release();
 // Existing physical bits are retained by the real post-CheckInput writer.
 touch(false,true,290,210);std::memset(data_0209f498+4,0,4);data_0209f498[4]=0x05;data_0209f498[6]=0x01;
 cstick_camera::after_check_input();assert(rd(4)==(right|5)&&rd(6)==(right|1));release();
 std::printf("PASS: 76800 hitboxes/pixels; %u command/gate cases; press/hold/release; ownership; real free orbit; physical bits retained\n",checks);
}
