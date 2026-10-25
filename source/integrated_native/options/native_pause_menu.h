#pragma once
#include "controller_options.hpp"
#include <cstdint>
#include <cstddef>
namespace enhance::native_pause {
enum class Page:uint8_t { Options,Controller,Audio,Display,Developer,Update,ConfirmDelete };
enum Request:uint32_t { SaveSettings=1,DumpFull=2,DeleteDumps=4 };
// DS key bits (A,B,_,_,Right,Left,Up,Down) and the touch point in native
// lower-LCD pixels (320x240), independent of how the DS image is scaled.
struct Input { uint32_t down=0;bool touch_pressed=false;int x=0,y=0;bool touch_down=false; };
enum Key:uint32_t { KeyA=1u,KeyB=2u,KeyRight=16u,KeyLeft=32u,KeyUp=64u,KeyDown=128u };
// Native 320x240 geometry shared by the art and the touch hit tests.
namespace layout {
inline constexpr int confirm_left=32,confirm_right=176,confirm_y=92;
inline constexpr int confirm_width=112,confirm_height=36;
inline constexpr int row_top=36,row_pitch=44,row_height=36,rows_per_page=3;
inline constexpr int row_left=10,row_right=310;
inline constexpr int value_left=155,value_width=137;      // value box / two 65-pixel toggles
inline constexpr int toggle_width=65,toggle_gap=10;
inline constexpr int arrow_left=142,arrow_right=298;
inline constexpr int slider_left=167,slider_right=236;    // Audio volume track
inline constexpr int page_down_x=140,page_down_y=170,page_down_w=40,page_down_h=20;
inline constexpr int footer_y=197,footer_h=34;
inline constexpr int ok_left=116,ok_right=205,back_left=270,back_right=310;
}
class Menu {
public:
 // Toggle rows draw ON/OFF buttons; their touch selects the touched state.
 bool toggle_row(unsigned row)const;
 void begin(const Settings&,unsigned sound,bool controller);
 void begin_general(const Settings& s,unsigned sound){begin(s,sound,false);general_=true;}
 bool update(Input); // true iff state/content changed
 Page page()const{return page_;} unsigned selected()const{return selected_;}
 unsigned count()const;unsigned first()const{return selected_<count()?(selected_/3)*3:((count()?count()-1:0)/3)*3;}
 const Settings& settings()const{return settings_;}unsigned sound()const{return sound_;}
 bool finished()const{return finished_;}bool accepted()const{return accepted_;}
 unsigned footer()const{return footer_;}
 const char* label(unsigned row)const;void value(unsigned row,char*,unsigned)const;
 uint32_t take_requests(){uint32_t r=requests_;requests_=0;return r;}
private:
 void activate(int);void back();void finish(bool);
 // Returns whether the touch belongs to the volume control; sets changed.
 bool update_volume_touch(const Input&,bool& changed);
 bool update_confirm(const Input&);
 bool update_touch(const Input&);
 bool toggle_value(unsigned row)const;
 void set_toggle(unsigned row,bool on);
 Settings settings_{},original_{};unsigned sound_=0,original_sound_=0,selected_=0,footer_=0;
 Page page_=Page::Options;bool finished_=false,accepted_=false,direct_controller_=false,volume_dragging_=false,general_=false;
 uint32_t requests_=0;
};
// The game's original menu may use any character tile, including our temporary
// sprite slots. Save actual VRAM/map owners once before entry and restore them
// before original parent reinitialization or direct Controller cleanup.
class VramBackup {
public:
 bool capture(uint8_t* chars,std::size_t chars_size,uint16_t* bg0,std::size_t bg0_entries,uint16_t* bg1,std::size_t bg1_entries);
 bool restore();
 void reset(){valid_=false;}
 bool saved()const{return valid_;}
private:
 uint8_t* chars_=nullptr;uint16_t* bg0_=nullptr;uint16_t* bg1_=nullptr;
 uint8_t character_bytes_[0x10000]{};uint16_t bg0_map_[1024]{},bg1_map_[1024]{};
 bool valid_=false;
};
// Native game's single-byte BMG encoding. Bounded ASCII labels; FF terminates.
unsigned encode(const char*,uint8_t*,unsigned capacity);
// Draw temporary DS BG0 tiles 0x300..0x323 and the 18x2 map cells.
// They are not globally free retail tiles: the native session backs them up.
bool render_volume_slider(unsigned volume,uint8_t* chars,std::size_t chars_size,uint16_t* map,std::size_t map_entries);
// Wider/taller cream panel used by Volume and the Update placeholder.
bool render_native_panel(unsigned y,uint8_t* chars,std::size_t chars_size,uint16_t* map,std::size_t map_entries);
}
extern "C" {
void sm64ds_native_pause_reset();
void sm64ds_native_pause_parent(unsigned state);
void sm64ds_native_pause_begin(int controller);
void sm64ds_native_pause_update();
int sm64ds_native_pause_render_cursor();
void sm64ds_native_pause_restore_parent();
void sm64ds_native_pause_controller_label(unsigned y);
uint32_t sm64ds_native_pause_take_requests();
}
