#pragma once
#include <cstdint>
namespace draw_material {
// Scope is one uninterrupted draw_batches call. Shader/matrix bindings do
// not modify these material registers. Shadow helpers can, so invalidate
// around every shadow batch and every experimental FramePlan.
struct Key {
 std::uintptr_t texture=0;
 uint32_t attributes=0,wrap=0,filter=0;
 bool textured=false;
 bool operator==(const Key& k)const{return texture==k.texture && attributes==k.attributes && wrap==k.wrap && filter==k.filter && textured==k.textured;}
};
class Cache {
 Key previous_{};bool valid_=false;
public:
 void invalidate(){valid_=false;}
 bool setup(const Key& k,bool safe){
  if(!safe){invalidate();return true;}
  if(valid_ && k==previous_)return false;
  previous_=k;valid_=true;return true;
 }
};
}
