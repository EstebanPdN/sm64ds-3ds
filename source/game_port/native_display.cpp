#include "sm64ds/game_draw.hpp"

extern "C" {
int func_02019018();
extern unsigned char data_0209d464;

// Same original submission gate and pending flag. Only the DS geometry-port
// store changes: the command now enters the native renderer's ordered stream.
void func_020190b8() {
    if(func_02019018()==0)return;
    sm64ds_draw_port(0x50,3);
    data_0209d464=1;
}
}
