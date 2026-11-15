#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int sm64ds_fresh_native_startup(void);
void sm64ds_fresh_native_math_configure(void);
int sm64ds_fresh_native_enabled(void);
int sm64ds_fresh_native_on_worker(void);
int sm64ds_fresh_native_allow_skip(void);
// Whether the original-frame hook skips presenting this VBlank (dry or late).
int sm64ds_fresh_native_skip_wanted(int capturing);
void sm64ds_fresh_native_skip(void);
int sm64ds_fresh_native_graphics(void);
void sm64ds_fresh_native_menu_preload_selector(unsigned language);
void sm64ds_fresh_native_begin(void);
void sm64ds_fresh_native_actor(uint64_t identity);
void sm64ds_fresh_native_head_canvas_mode(int mode);
void sm64ds_fresh_native_scene_changed(void);
void sm64ds_fresh_native_projection(const int32_t* matrix16);
void sm64ds_fresh_native_view(const int32_t* affine12);
void sm64ds_fresh_native_look_at(const void* at,const void* up,const void* eye,int install,void* output);
void sm64ds_fresh_native_legacy_words(uint32_t count);
void sm64ds_fresh_native_legacy_write(uint32_t count,uint32_t kind,uintptr_t caller,uint32_t address);
void sm64ds_fresh_native_writer_probe(uint32_t kind,uintptr_t caller,uint32_t address,uint32_t width);
void sm64ds_fresh_native_reset_graphics(void);
void sm64ds_fresh_native_clear_colour(uint16_t rgb,int alpha,int depth,int polygon_id,int fog);
int sm64ds_fresh_native_present(int unmigrated_2d);
// Explicit diagnostic mode can continue simulation after an UNPUBLISHED
// producer frame. It never converts a refused frame into a successful draw.
int sm64ds_fresh_native_diagnostic_hold(void);
// Synchronous diagnostic ownership: retire both presentations and fence the
// GPU before CPU memory/LCD capture; no original renderer is initialized.
int sm64ds_fresh_native_diagnostic_begin(void);
int sm64ds_fresh_native_diagnostic_capture(const char* directory);
void sm64ds_fresh_native_diagnostic_end(void);
void sm64ds_fresh_native_shutdown(void);
// BUILD124 Old 3DS core layout, decided by player_present when it asks APT
// for core-1 time: render worker on core 1 (80% granted, audio on core 0) or
// the BUILD123 layout (audio on core 1 at 30%, render on core 0).
int sm64ds_fresh_native_render_core1_wanted(void);
void sm64ds_fresh_native_core_layout(int render_core1,int audio_core);
#ifdef __cplusplus
}
#endif
