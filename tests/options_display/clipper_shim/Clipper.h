#pragma once
#include <cstdint>
using Fix12i=int32_t;
struct Vector3 {int32_t x,y,z;};
struct Clipper {
 uint32_t vptr;
 Vector3 mPlaneNormals[4];
 unsigned char pad[0x18];
 int32_t mAspectRatio,mNearZ,mFarZ;
 uint16_t mFovAngle;
 void Func_0201559C();
};
static_assert(__builtin_offsetof(Clipper,mAspectRatio)==0x4c);
