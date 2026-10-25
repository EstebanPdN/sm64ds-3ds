#pragma once
#include <cstdint>
enum {KEY_A=1,KEY_START=8,KEY_SELECT=4,KEY_R=256,KEY_L=512,KEY_TOUCH=1<<20,
 KEY_DLEFT=32,KEY_DRIGHT=16,KEY_DUP=64,KEY_DDOWN=128};
struct circlePosition {short dx,dy;};
extern circlePosition test_pad;
inline void hidCircleRead(circlePosition* p){*p=test_pad;}
