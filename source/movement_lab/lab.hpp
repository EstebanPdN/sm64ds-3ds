#pragma once
#include <cstdint>
struct Player;
namespace movement_lab {
enum class Mode : uint8_t { Original, Hybrid, N64 };
void frame(unsigned scene,unsigned updates);
uint32_t input(uint32_t held,uint32_t down);
// Reuse the published image until a visible field changes.
bool render(uint32_t* pixels);
bool step(Player* player);
void commit(Player* player);
}
