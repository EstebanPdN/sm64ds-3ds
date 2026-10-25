#include "sm64ds/game_input.hpp"

extern "C" {
extern sm64ds::GamePad data_020a0e58[];
extern sm64ds::GameTouch data_020a0de8[];
extern sm64ds::GameControl data_0209f498[];
extern uint8_t data_0209f2d8,data_0209d660,data_0209f21c;
extern uint32_t data_0209caa0[];
extern int32_t data_0209fc48;
int16_t _ZN4cstd5atan2E5Fix12IiES1_(int32_t,int32_t);
// objcopy renames only the definition in an audited copy of the original TU.
// Every original caller continues to reach the wrapper below.
void sm64ds_original_check_input(void* stage);
}
namespace { sm64ds::Stick current_stick; }
namespace sm64ds {
void publish_game_input(const GameInputSample& sample) {
    data_020a0e58[0]=sample.pad;
    data_020a0de8[0]=sample.touch;
    current_stick=sample.frame.movement;
}
}
extern "C" void _ZN5Stage10CheckInputEv(void* stage) {
    sm64ds_original_check_input(stage);
    if(data_0209f21c && sm64ds::game_accepts_movement(data_0209f2d8,
        data_0209caa0[2],data_0209d660,data_0209fc48)) {
        // Preserve mapped buttons and original touch/camera logic; only the
        // movement fields change when the Circle Pad has nonzero deflection.
        sm64ds::apply_game_analog(data_0209f498[0],current_stick,
                                  _ZN4cstd5atan2E5Fix12IiES1_);
    }
}
