#pragma once
#include "sm64ds/input.hpp"
#include <cstddef>

namespace sm64ds {
// Byte-exact records read by Stage::CheckInput. These are data layouts, not
// reinterpretations of a libctru input structure.
struct GamePad { uint16_t held, pressed; };
struct GameTouch { uint8_t touched, tapped, x, y; };
struct GameControl {
    int16_t dx,dy;
    uint16_t held,pressed;
    int16_t magnitude,nx,ny,angle;
    uint8_t tx,ty,delay,count,touching,pad15,mode,pad17;
};
static_assert(sizeof(GameControl)==0x18 && offsetof(GameControl,magnitude)==8
              && offsetof(GameControl,touching)==0x14);
static_assert(sizeof(GamePad)==4 && sizeof(GameTouch)==4);
struct GameInputSample { InputFrame frame; GamePad pad{}; GameTouch touch{}; };

// Sample every display frame, consume only on simulation ticks. A press that
// begins and ends between two ticks is delivered once, including a brief tap.
class GameInputLatch {
public:
    void sample(const InputFrame& input);
    GameInputSample consume();
    void reset();
private:
    InputFrame latest_{};
    uint32_t pending_press_=0,pending_release_=0;
    bool touch_edge_=false,previous_touch_=false,delivered_touch_=false;
    uint16_t edge_x_=0,edge_y_=0;
};

bool game_accepts_movement(uint8_t mode,uint32_t save_flags,
                           uint8_t pause_state,int32_t cutscene);
// Original cstd::atan2 supplies the game's exact angular quantization.
using GameAtan2 = int16_t (*)(int32_t,int32_t);
bool apply_game_analog(GameControl& control,Stick stick,GameAtan2 atan2);

#ifdef __3DS__
// Call once per game tick, before original scene/Stage behavior. The title
// communications bridge and C-stick camera state machine remain separate.
void publish_game_input(const GameInputSample& sample);
#endif
}
