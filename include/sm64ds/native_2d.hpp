#pragma once
#include <cstdint>
#include <string>
namespace sm64ds {
// 256x192 original pixels in 256x256 PICA texture storage. Blank rows are zero.
struct Native2DLayer {uint32_t pixels[256*256]{};};
struct Native2DFrame {
    Native2DLayer underlay,overlay,bottom;
    bool main_on_top=true,has_3d=false;
};
// Uses the existing upstream 2D compositor against owned native_video memory.
// 3D blending with BG/OBJ and scanline-dependent effects are pending.
void native_2d_scanout(Native2DFrame& frame);
// Source A composited layers from engine A before MASTER_BRIGHT and display
// selection. Geometry remains separate: has_3d asks the renderer to provide
// actual BG0 3D pixels, regardless of the display mode. No 3D pixels are made
// here. Underlay/overlay are updated; bottom is untouched. Does not write video
// state. Forced blank and unsupported modes fail explicitly; ignore the output
// on failure. The ordinary presentation route is independent of this tap.
bool native_2d_capture_layers(Native2DFrame& frame, std::string& error);
}
