# Architecture

The maintained executable combines a pinned native SM64DS backend with
project-owned preparation scripts and adapters. It is a native ARM11/PICA200
application; it does not run a DS emulator. The historical viewer/runtime
under `source/ctr/` and `source/game_port/` is a separate development path.

The [native renderer](../source/native_renderer/README.md) under
`source/native_renderer/` draws the game from an immutable scene description
instead of replaying DS GPU commands. It ships in the native-test CIA (title
`000400000F536500`, BUILD 86 onward), built by the
[native pipeline](../tools/native_renderer/pipeline/README.md); the 2D layers
still pass through the original PPU and a bridge. The integrated package
built by `tools/build_integrated_native.py` keeps the provider's renderer and
remains the comparison route. Design notes: [NATIVE-RENDERER.md](NATIVE-RENDERER.md).

## Build flow

```mermaid
flowchart LR
    Pin[Byte-pinned native provider] --> Copy[Generated working copy]
    Local[Project adapters and checked preparers] --> Copy
    ROM[Private compatible ROM] --> Catalog[Local catalogs and resources]
    Copy --> Gates[Host and ARM gates]
    Catalog --> Gates
    Gates --> Build[ARM11 build and executable audit]
    Build --> Candidate[Closed candidate with source and hashes]
    Candidate --> Package[Verified local CIA or 3DSX]
```

`tools/build_integrated_native.py` owns orchestration. It verifies source
pins, reserves a BUILD number, snapshots inputs, runs gates and freezes the
result. `tools/prepare_integrated_*.py` validate known source contracts before
editing the generated copy. Packaging consumes a closed candidate and checks
its executable identity; it does not rebuild or publish it.

## Startup and runtime

The generated `game_entry.cpp` initializes the frontend, applies settings,
selects a valid companion or prepares it from the user's ROM, and enters the
native backend. Boot-time containers use the system allocator: the global
C++ allocator belongs to the retail game heap and is unavailable until that
heap is initialized. BUILD 24 removes a missing-settings fallback allocation
that violated this ordering.

The original game advances its simulation at its existing 30 Hz cadence.
`render_60.hpp` and presentation hooks can insert eligible visual midpoints
without advancing game logic twice. Pauses, menus and held frames have their
own invalidation rules. A displayed frame rate is not proof of physical
timing on every console model.

## Ownership boundaries

| Component | Owns and preserves |
| --- | --- |
| `rom_setup_3ds.hpp`, `rom_bootstrap.hpp`, `packed_builder.hpp` | Bounded ROM validation/preparation, cancellation and companion publication. |
| `packed_assets*.hpp` | Companion member index and shared read access; member offsets replace thousands of SD files. |
| `options/enhance_settings.*`, `atomic_file.hpp` | Validated settings and backup-first replacement without early retail allocations. |
| `options/native_pause_*`, `native_lower_*` | Menu model, touch actions and cached lower-screen images. |
| `options/overlay_publication.hpp` | Producer/consumer generations; hiding an image does not release a slot still read by a worker. |
| `options/cstick_camera.*`, `native_gameplay_camera.hpp` | Camera input, dead zones and original-game handoff. |
| `movement/native_motion.cpp`, `world_session.hpp` | Actor/session lifetime, input history, solver continuity and DS handoffs. |
| `options/audio_gain.hpp`, `audio_diagnostics.hpp` | Stereo gain ramp and opt-in counters; original mixer/NDSP retain PCM/queue ownership. |
| `options/console_profile.*`, `level_files.hpp` | Model-aware budgets and level prefetch policy. |
| `native_diagnostics.hpp`, `options/diagnostic_*`, `dump_*` | Private runtime captures, counters and diagnostic presentation. |

The backend owns actor logic, source resources, GPU rendering and audio
services; local hooks adapt the boundary. N64 movement is optional and must
yield for unsupported actor states, abilities, damage, water and scripted
actions. Switching profiles must not erase an owned solver's jump history.

## Threads, caches and teardown

Console profiles select available worker cores and cache/preload budgets.
The presentation consumer and game producer exchange completed images using
the publication protocol. Shutdown joins workers before releasing resources
they can still reference. GPU fences and cache maintenance remain part of
correctness, even when profiling identifies their cost.

Level file lists can be prefetched into bounded RAM; shared companion reads
avoid repeatedly opening the same SD file. Mesh/material/uniform caches and
2D UI caches reduce repeated work, with invalidation tied to their actual
inputs. Changes to cache keys, routing or render order require differential
checks, not only an FPS comparison.

The diagnostic capture settles presentation work, records displayed buffers
and exports readable mappings and VRAM. GPU, DSP and operating-system state
are not an atomic whole-machine snapshot. See [Diagnostics](DIAGNOSTICS.md)
and the detailed [adapter contracts](DEVELOPMENT.md).

## Native renderer presentation

The game keeps its 30 Hz update. After each update the game thread seals a
scene capture (two stores alternate) and hands it to a render worker; when
the worker is still busy the update shows no new image instead of queueing.

| Console | Render worker | Audio mixer |
| --- | --- | --- |
| Old 3DS | Core 0, beside the game thread, one priority step below it | Core 1, capped at 30 % |
| New 3DS | Core 2 | Core 1 |

**Lock FPS** (pause menu, Display) chooses how many images an update shows:

- **30**: one exact image per update. Default on the Old 3DS.
- **60**: an interpolated image (part matrices and skin positions halfway
  between two updates) and then the exact image, one LCD refresh apart.
  Default on the New 3DS.
- **Unlocked**: the same pair without waiting for the LCD.

On the Old 3DS both images share the game's core. The exact image is
recorded into a second command buffer while the GPU draws the interpolated
one, then appended to the GPU queue; it reuses the interpolated image's
uploads and draws only the screen it presents. A budget guard interpolates
only when the update and both images fit in 92 % of the 33.4 ms period, and
falls back to one image per update for 30–480 updates after a missed pair.
Audio command batches that find the mixer busy are delivered after the image
is handed over, so the game thread does not wait behind the mixer.

Every cut that keeps an older path can be switched back on the console with
`sdmc:/3ds/sm64ds/sin-recortes.txt` (and `audio-espera.txt` for audio), so
the same build measures both. These paths are checked for identical output
on the host and in Azahar; their console cost is measured by the
[test mode](TEST_MODE.md), not assumed. Dated measurements and decisions:
[NATIVE_60FPS_BUDGET.md](NATIVE_60FPS_BUDGET.md).
