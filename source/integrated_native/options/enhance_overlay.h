#pragma once
#include "enhance_settings.h"
#include <cstdint>
namespace enhance {
struct Diagnostics { double fps=0,frame_ms=0;unsigned heap_free=0,linear_free=0;bool new_model=false;unsigned render_width=256,render_height=192;bool fallback=false,dumps_enabled=false; };
void render_diagnostics(uint32_t*,const Diagnostics&);
// Opaque original Mario DS pattern, shared by front-menu options and diagnostics.
void render_menu_background(uint32_t*);
int draw_game_text(uint32_t*,int,int,int,int,const char*,bool title_font=false);
// Compatibility helper for the renderer's FPS text (original 8x8 bitmap).
int draw_text(uint32_t*,int,int,int,int,const char*,uint32_t,const uint8_t*,int scale=1);
}
