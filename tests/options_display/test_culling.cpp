#include "Clipper.h"
#include "enhance_settings.h"
#include "display_culling.hpp"
#include <cassert>
#include <initializer_list>

#include <cmath>
#include <cstdio>
#include <cstring>
namespace enhance {Live& live(){static Live state;return state;}}
extern "C" void __wrap__ZN7Clipper13Func_0201559CEv(void*);
extern "C" void __real__ZN7Clipper13Func_0201559CEv(void* c){static_cast<Clipper*>(c)->Func_0201559C();}
extern "C" {short data_02082214[8192]{};}
extern "C" int _ZN4cstd4fdivEii(int a,int b){return int((int64_t(a)<<12)/b);}
extern "C" void CrossVec3(Vector3* a,Vector3* b,Vector3* c){
 *c={int32_t((int64_t(a->y)*b->z-int64_t(a->z)*b->y)>>12),int32_t((int64_t(a->z)*b->x-int64_t(a->x)*b->z)>>12),int32_t((int64_t(a->x)*b->y-int64_t(a->y)*b->x)>>12)};
}
extern "C" void NormalizeVec3(Vector3* a,Vector3* c){
 const double length=std::sqrt(double(a->x)*a->x+double(a->y)*a->y+double(a->z)*a->z);
 *c={int32_t(std::lround(a->x*4096/length)),int32_t(std::lround(a->y*4096/length)),int32_t(std::lround(a->z*4096/length))};
}
bool visible(const Clipper& c,Vector3 p){
 if(-p.z<c.mNearZ || -p.z>c.mFarZ)return false;
 for(auto n:c.mPlaneNormals)if(int64_t(p.x)*n.x+int64_t(p.y)*n.y+int64_t(p.z)*n.z>0)return false;
 return true;
}
int main(){
 data_02082214[0x2000/16*2]=2896;data_02082214[0x2000/16*2+1]=2896;
 Clipper c{};c.mAspectRatio=5461;c.mNearZ=4096;c.mFarZ=409600;c.mFovAngle=0x2000;
 unsigned checks=0;
 for(uint8_t mode:{0,1,2}){
  enhance::live().screen_mode=mode;
  for(unsigned repeat=0;repeat<100;++repeat){
   __wrap__ZN7Clipper13Func_0201559CEv(&c);
   assert(c.mAspectRatio==5461 && c.mNearZ==4096 && c.mFarZ==409600 && c.mFovAngle==0x2000);++checks;
  }
  // At depth8 the DSfrustumhalfwidth=10.666;wide=13.333.
  for(int sign:{-1,1}){
   assert(visible(c,{sign*12*4096,0,-8*4096})==(mode==1));++checks;
   assert(!visible(c,{sign*14*4096,0,-8*4096}));++checks;
   assert(visible(c,{sign*10*4096,0,-8*4096}));++checks;
  }
  // Vertical,nearandfar rejection remain identical in all modes.
  assert(!visible(c,{0,9*4096,-8*4096}));++checks;
  assert(!visible(c,{0,0,-4095}));++checks;
  assert(!visible(c,{0,0,-409601}));++checks;
 }
 assert(display_culling::aspect(INT32_MAX,1)==INT32_MAX);++checks;
 assert(display_culling::aspect(-10,1)==-10);++checks;
 std::printf("Original Clipper rebuild+wrapper: %u edge,repeat,vertical,near/far checks passed\n",checks);
}
