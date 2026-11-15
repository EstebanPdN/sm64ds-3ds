# Native renderer

A Citro3D renderer that draws the game's 3D scene and 2D layers from an
immutable, renderer-owned description of each image instead of replaying the
DS GPU command stream. The game-side producers that fill that description
live in [`../integrated_native/renderer/`](../integrated_native/renderer/);
both directories are copied into the generated checkout by
`tools/native_renderer/pipeline/refresh_native_overlay.py`.

Nothing here includes game structures: the boundary is
[`contracts.hpp`](contracts.hpp) (meshes, materials, matrices, handles and
error codes). Every type is either owned by the render thread or borrowed
for one synchronous call, as each header states.

## Frame flow

1. The game thread captures one update into a `SceneFrame`
   ([`scene_frame.hpp`](scene_frame.hpp)) and seals it.
2. The render worker leases the sealed frame as a `SceneView` and calls
   `SceneRenderer::draw` ([`scene_renderer.hpp`](scene_renderer.hpp)):
   validation, resource residency, dynamic streams, batch planning, 3D
   geometry, then 2D composition of both screens.
3. `Citro3DBackend` ([`citro3d_backend.hpp`](citro3d_backend.hpp)) owns every
   GPU resource, issues the PICA200 state and submits the frame.
4. `ScreenCompositor` ([`screen_compositor.hpp`](screen_compositor.hpp))
   places the scene and the 2D layers on the two LCD targets.

With Lock FPS 60 or Unlocked, `PoseHistory`
([`scene_interpolation.hpp`](scene_interpolation.hpp)) builds an interpolated
image between two updates (part matrices and skin positions only). On the
Old 3DS the exact image is then recorded while the GPU draws the interpolated
one (`SceneRenderer::draw_second`). See
[docs/ARCHITECTURE.md](../../docs/ARCHITECTURE.md#native-renderer-presentation).

## Module map

| Area | Files |
| --- | --- |
| Contracts and scene description | `contracts`, `scene_frame`, `game_frame_state`, `scene_capture`, `screen_scene`, `image_layer`, `affine_image`, `interface_image` |
| GPU backend | `citro3d_backend`, `citro3d_program_cache`, `native_uniforms`, `pica_state`, `indexed_draw_packet`, `*.v.pica` |
| Scene and screens | `scene_renderer`, `screen_compositor`, `canvas_windows` |
| Assets and residency | `model_bank`, `model_catalog`, `native_model`, `mesh_groups`, `image_bank`, `image_catalog`, `native_images`, `canvas_catalog`, `bank_crc` |
| Models, poses and batching | `model_instance`, `game_model_pose`, `game_material`, `native_batch`, `native_projection`, `native_quads` |
| Lighting | `lighting`, `lighting_palette`, `lighting_uniforms`, `vertex_lighting` |
| Texture coordinates | `fixed_uv`, `normal_uv` |
| 2D and effects | `game_sprite_geometry`, `game_sketch_geometry`, `game_particle_geometry`, `game_wave_grid`, `dialogue_text`, `shadow_volumes`, `ds_fog` |
| Interpolation | `scene_interpolation` |
| Diagnostics | `writer_provenance`, `frame_timeline` |
| Console A/B switch | `perf_switch` |
| ARM11 kernels | `affine_compose_arm11.S`, `ds_math_unit.h` |

## Switches that keep an older path

Each reads a file in `sdmc:/3ds/sm64ds/` once at start-up, so one console
can measure both paths with the same build:

- `sin-recortes.txt`: the BUILD 133 paths of every exact cost cut
  (`sm64ds::perf::cuts()`), including the overlapped second image.
- `audio-espera.txt`: the game thread waits for the audio mixer again.

## Checks

Host tests are registered in [`cmake/NativeRenderer.cmake`](../../cmake/NativeRenderer.cmake)
and described in [tests/README.md](../../tests/README.md). Exactness gates
compare the cut paths against reference copies of the old code; emulator
screenshot comparisons use the tools in
[`tools/native_renderer/pipeline/`](../../tools/native_renderer/pipeline/README.md).
