# Native renderer: game-side adapters

These files connect the running DS game to the
[native renderer](../../native_renderer/README.md). They read the game's own
state at the points where it would have issued DS GPU or 2D work, turn it
into the renderer's immutable scene description, and run the render worker.
The preparer `tools/prepare_integrated_native_renderer.py` adds the matching
hooks and link wrappers to the generated provider checkout.

| Area | Files |
| --- | --- |
| Runtime, worker and presentation | `game_runtime.cpp`, `game_runtime.h` |
| 3D model capture | `scene_producer`, `model_components_hooks`, `model_load_hook`, `game_materials`, `game_lighting`, `texture_sequence_hooks`, `game_native_math` |
| Menus and 2D | `menu_backgrounds`, `menu_backdrops`, `menu_display_state`, `menu_oam_hooks`, `menu_sprite_hooks`, `menu_obj_catalog`, `menu_cell_capture`, `menu_model_hooks`, `bridge_composition`, `ds_layer_census` |
| Dialogue | `dialogue_hooks`, `dialogue_window_view` |
| Particles | `particle_hooks` |
| Test mode | `test_mode.cpp`, `test_mode.h`, `test_mode_game.cpp`, `test_mode_script.hpp` |
| Guards and diagnostics | `game_graphics_guard`, `game_writer_probe` |

## Runtime responsibilities (`game_runtime.cpp`)

- Start the render worker: on the Old 3DS it shares the game's core with a
  lower priority; on the New 3DS it runs on core 2.
- Seal one `SceneFrame` per game update (two stores alternate) and hand it
  to the worker; drop an update instead of queueing when the worker is busy.
- Apply Lock FPS (30, 60 or Unlocked) from the live settings; `SM64DS_FPS_LOCK`
  forces it in development builds.
- Decide whether an interpolated image fits: on the shared Old 3DS core the
  update plus both images must fit in 92 % of the 33.4 ms period, or the
  update shows one exact image.
- Write the test-mode reports: frame times, stages, GPU work, audio waits and
  the interpolation counters described in [docs/TEST_MODE.md](../../../docs/TEST_MODE.md).

## Test mode

SELECT+START starts an automatic tour of every level; R+SELECT+START measures
the current spot. `test_mode_script.hpp` is the platform-free part (scripts,
level tables, state file and run state machine) and is covered by host
tests; `test_mode.cpp` writes reports and dumps to the SD card.
