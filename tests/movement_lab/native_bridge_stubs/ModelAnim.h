#pragma once
#include <cstdint>
// Host service stand-ins, never a claim about the ARM DS object ABI.
struct Animation {int currFrame=0,speed=4096;unsigned numFramesAndFlags=32;};
struct ModelAnim:Animation {void* file=nullptr;unsigned clip=~0u;};
