#include "sm64ds/game_input.hpp"

#include <cstring>

extern "C" {
extern sm64ds::GameTouch data_020a0de8[4];

// Original func_0203bbc0 configures DS ARM7 SPI sampling and reads DS firmware
// calibration from 0x027ffc80. NativeFrame publishes the 3DS HID calibrated
// touch sample in the game's 256x192 coordinate space; neither DS service is
// involved. Only clear the actual four-player touch records here. Do not
// manufacture SPI replies or mark a wireless backend initialized.
void func_0203bbc0() { std::memset(data_020a0de8, 0, sizeof(data_020a0de8)); }
}
