#pragma once
#include "screen_scene.hpp"

namespace sm64ds::render {
// Native prepared canvas: translate within its finite domain, clip, then
// repeat that domain across a viewport. All values are integer canvas pixels.
// Transparent fill outside the translated source is a caller-owned contract.
// No source tile words, pixels, register values or GPU handles enter this API.
struct CanvasCrop {
    std::uint16_t width=0,height=0,viewport_width=0,viewport_height=0;
    std::int32_t translate_x=0,translate_y=0,scroll_x=0,scroll_y=0;
};
struct CanvasWindow {
    std::int32_t x=0,y=0; // translated whole-canvas origin; retain full mesh
    std::uint16_t left=0,top=0,right=0,bottom=0; // exclusive viewport clip
};
struct CanvasWindows {
    std::array<CanvasWindow,4> window{};
    std::uint8_t count=0;
};
// Viewports no larger than the canvas require at most four windows. The
// function allocates/copies no image or geometry data. Invalid input leaves
// output untouched; completely cropped sources succeed with count zero.
Error canvas_windows(const CanvasCrop&,CanvasWindows&) noexcept;
struct CanvasPlacement {ScreenRect rectangle;ScreenClip clip;};
// Project an integer window into an on-screen physical viewport at scale>=1.
// A 1/16 physical-pixel origin bias selects the increasing source tile at
// exact pixel-centre seams in PICA's rotated rasterization. Atlas UVs retain
// their separately prepared nearest bias. Geometry and UVs stay resident.
Error canvas_placement(const CanvasCrop&,const CanvasWindow&,Screen,
                       const ScreenRect& viewport,CanvasPlacement&) noexcept;
}
