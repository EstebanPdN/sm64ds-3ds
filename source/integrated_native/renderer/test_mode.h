#pragma once
// SELECT+START automated hardware test (see test_mode_script.hpp).
// Game thread only unless stated otherwise.
#include <cstdint>
#include <cstddef>
extern "C" {
// Startup, before the renderer reads its environment: resumes a pending run.
void sm64ds_test_mode_boot(void);
// A thread of this boot could not be created (render worker or game thread):
// the tour keeps this level for the next boot and the game returns to HOME.
void sm64ds_test_mode_boot_failed(const char* what);
// After the real hidScanInput: chord detection and START cancellation.
void sm64ds_test_mode_scan(std::uint32_t real_held,std::uint32_t real_down);
// Start of every original game update: script, level entry and reports.
void sm64ds_test_mode_update(void);
// Scripted controller for this update; false = use the real one.
int sm64ds_test_mode_keys(std::uint32_t* held);
int sm64ds_test_mode_circle(int* x,int* y);
int sm64ds_test_mode_cstick(int* x,int* y);
int sm64ds_test_mode_touch(int* x,int* y);
// Render worker: 256x16 status strip and 320x240 final panel (ARGB8888).
// Returns the revision of the published image, 0 when there is none.
std::uint32_t sm64ds_test_mode_strip(const std::uint32_t** pixels);
std::uint32_t sm64ds_test_mode_panel(const std::uint32_t** pixels);
// Implemented by the renderer runtime: one report (CSV, summary, BMPs) as
// files named directory.<kind> (BUILD131: a file stem, no folder per report).
// The caller holds the report pause (1 = drain and start, 0 = flush and end)
// around all of a report's SD work, folders included.
int sm64ds_fresh_native_report_pause(int begin);
int sm64ds_fresh_native_test_report(const char* directory,std::uint64_t window_tick,const char* title,
    char* summary,std::size_t summary_size);
// Renderer runtime: snapshot the mixer timing counters at a window start
// (the report's audio section covers the same window).
void sm64ds_fresh_native_audio_window(void);
// Game side (test_mode_game.cpp, decomp headers only).
struct Sm64dsTestPlayer {int present,level,area,character,body,control,dialogue,paused,pause_locked;std::int32_t x,y,z;int angle;};
void sm64ds_test_game_observe(unsigned scene,Sm64dsTestPlayer* out);
int sm64ds_test_game_teleport(unsigned scene,int area,std::int32_t x,std::int32_t y,std::int32_t z,int angle);
void sm64ds_test_game_start_file(int level,int area);
void sm64ds_test_game_set_character(int character);
// BUILD131: live actors (zone spots and estado_juego.txt). Returns the live
// total; at most capacity entries are written. "player" marks player actors.
struct Sm64dsTestActor {std::uint32_t unique;std::uint16_t id;std::int8_t area;std::uint8_t alive,player;std::int32_t x,y,z;};
unsigned sm64ds_test_game_actors(Sm64dsTestActor* out,unsigned capacity);
// The game's own ground probe below (x,y,z); 0 when there is no floor.
int sm64ds_test_game_floor(std::int32_t x,std::int32_t y,std::int32_t z,std::int32_t* floor_y);
struct Sm64dsTestPlayerState {std::int32_t horizontal_speed,vertical_speed;int health,underwater,shallow_water,in_air,floor_class;
    unsigned state_flags,cap_flags,powerup_timer;std::uintptr_t state;};
int sm64ds_test_game_player_state(unsigned scene,Sm64dsTestPlayerState* out);
// Renderer runtime (BUILD131): the files of a deep dump, see docs/MODO_PRUEBA.md.
// arm: 1 before the frames to capture, 0 to release an unused capture.
void sm64ds_fresh_native_test_dump_arm(int arm);
// Caller holds the report pause. flags: 1 whole process memory (memoria.vm),
// 2 DS video memory, 4 reserved, 8 2D planes/sprite memory.
// BUILD132 never reads physical VRAM or hardware GPU registers; RAM excludes devices.
int sm64ds_fresh_native_test_dump(const char* directory,int flags,char* summary,std::size_t summary_size);
}
