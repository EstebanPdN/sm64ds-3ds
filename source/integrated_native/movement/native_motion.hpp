#pragma once
#include <cstdint>
#include "motion_buttons.hpp"
struct Player;
namespace native_motion {
enum class Mode:uint8_t {Original,Hybrid,N64};
void frame(unsigned scene,unsigned updates);
uint32_t input(uint32_t held,uint32_t down);
bool step(Player*);
void commit(Player*);
}
extern "C" bool native_motion_owns_position();
