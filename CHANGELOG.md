# Changelog

Public betas are published as GitHub releases. Local BUILD numbers identify
test candidates, not releases or a promise of full-game compatibility.

## beta-02 — BUILD 148, 2026-11-27

- Fixed the paintings: they load their picture again and face the right way.
- The castle on the file-select screen fills the whole upper screen.
- Fixed several stutters during play.
- The opening cutscene's bottom map has no clouds and runs faster.
- The START menu works with the D-Pad, A and B.
- Stereoscopic 3D works from the slider, with no option to turn on.
- L + R + A dumps take about a second and save four files in `dumps/`.

## Unreleased — source publication

- Document the integrated game as the main development path, with separate
  maps for the historical runtime and model viewer.
- Add current build, architecture, testing, SD-data, compatibility and
  diagnostic guides, preserving dated engineering records.
- Publish the pinned native source and motion-library submodule, with
  provenance and component-specific notices.
- Allow explicit motion-source and animation paths, removing the requirement
  for the maintainer's sibling MK64 project folder.
- Add repository checks, ROM-free CI, issue forms and contribution guidance.
- Keep private game inputs, dumps, generated packages and design iterations
  outside the public Git tree. No binary release is created.

## Local BUILD 142 — Old 3DS and New 3DS profiles, 2026-11-20

Azahar-checked; physical qualification pending.

- New 3DS profile: the 124 MB mode gets a 32 MiB linear heap (Old keeps 16)
  through a mode-aware `__system_allocateHeaps`, and a 20 MiB native GPU
  budget (Old 12). The bottom map's render surfaces must live in VRAM; with
  the New 3DS's supersampled scene the top HUD planes left 1.5 MB in pieces
  and the map failed on every try ("Native map publication failed:
  allocation failed", since BUILD 140), sending the opening's 2D back to the
  CPU (10 ms per update). A HUD plane now gives its VRAM target up for the
  map and takes its CPU path. Settings and save writes run on core 1 of a
  New 3DS (game core 0, audio core 1, render core 2; core 3 belongs to the
  system). Azahar New 3DS: opening castle 37.5 -> 58.9 FPS, gardens
  41.6 -> 59.0, journey average 50.9 -> 55.6.
- Old 3DS profile (cost cuts, same pictures): the opening's bottom map
  scrolled its GPU cloud layers through the full PPU path every update
  (palette conversion and layer compares); a scroll-only change now just
  updates the layers' offsets (3.8-4.7 ms -> 0.4-0.8 ms per preparation at
  half clock). Semi-transparent sprites the GPU blends take the row fast
  path of opaque ones (the ~40 cloud sprites). A held map now hands its
  buffer state to the copy instead of forcing a full redraw after every held
  image. Map textures are tiled a row of 8 texels at a time.
  Azahar Old 3DS at 25 % clock: opening average 16.5 -> 23.1 FPS.
- Diagnostics: per-phase game-thread costs (`Native present phases`,
  `fases_presentacion` in the test reports), the map's preparation costs,
  the dynamic render stage and which map step fails.
- Tests: the native map test runs the memo patch as the preparer orders it
  and covers scroll-only updates and cloned held buffers; the BUILD 133 PPU
  comparison links again (BUILD 141 changed the signature) and accepts a
  precise band mask where BUILD 133 had forgotten the buffer.

## Local BUILD 141 — the opening's bottom screen on the GPU, 2026-11-18

Azahar-checked; physical qualification pending.

- The opening's scrolling cloud layers (BG0/BG1, and the tower's fog BG2)
  are drawn by the GPU from copies of their rasters with the scroll offsets,
  instead of being merged into a 256x192 plane, copied into the foreground
  and uploaded at every refresh. Sprites are split between a plane behind
  the layers and the foreground in front of them. Same textures (512x512
  RGBA8, repeat) and draw state as the rotating map: the GPU lists have no
  state the console has not run. Picture identical to BUILD 140 in Azahar.
- A layer copy is made only when its tiles or blend change (a fog fade step);
  a layer that changes on consecutive updates stays on the CPU path. Copies
  live in linear memory and are freed at every scene change.
- The bottom map is refreshed every update when there is room, at 10 Hz when
  the update and its image already fill the 30 Hz period; a held map skips
  its uploads and its redraw.
- Azahar, CIA (80 MB), Old 3DS at 50 % clock, Lock 60, opening windows: most
  at 35-57 FPS (BUILD 140: 26-33), the heaviest stretch 25-30 as before.

## Local BUILD 140 — console report fixes (BUILD 139), 2026-11-18

Azahar-checked; physical qualification pending.

- Fix the BUILD 139 regression that left the castle grounds without terrain
  or sky and Luigi without a body: a 2.5 MB second PPU cache way, allocated
  in an early 2D scene and never freed, exhausted the 80 MB heap and every
  model bank load failed. Extra ways are now released at each scene change
  and when a bank load runs out of memory; journey reports count failed
  loads (`cargas_fallidas`) and flag them.
- Shadows: the DS rule, per material. Everything drawn with polygon ID 1
  (characters, Yoshi's whole body, trees) is never darkened by a shadow;
  BUILD 139 only protected skinned joints, so Yoshi's rigid parts stayed dark.
- Lock FPS 60/Unlocked on the Old 3DS: the budget counts CPU time and the
  second image's GPU/LCD waits separately, and that wait no longer drops the
  next update (BUILD 139 still ran at 29.9 FPS on the console).
- Title: the star model, its images and the title layouts are loaded before
  the title starts (the star appeared abruptly after its sound).
- File 1 transition: the white fade is written to the DS blend registers
  when the native menu ends, so the opening starts from white instead of
  flashing the previous screen.
- Opening: the castle tower's fog and plain translucent sprites over the
  rotating map are blended by the GPU (BUILD 139 blended every fog pixel on
  the CPU: 58-69 ms per refresh). Up to 3/255 per channel differs from the
  DS's 6-bit blend; `sin-recortes.txt` restores the CPU blend. A held map
  image no longer reuses the other buffer's sprite boxes.
- Developer dumps: `memory.vm` is written as SM64VMZ1 (LZ4 blocks of 512 KiB,
  zero blocks skipped; about 11-13 MB instead of 90 MB), with a 2 MiB
  workspace in linear memory released after the dump. `tools/vm_packed.py`
  and the existing readers expand it.

## Local BUILD 139 — console report fixes (BUILD 138), 2026-11-18

Azahar-checked; physical qualification pending.

- Options menus: OK or Back on a category (Display, Audio, Controls...)
  returns to the Options list on that row; only OK on the list closes it.
- Option screen changes no longer leave a gap without any native image (the
  original DS layers showed through as a ghost behind the buttons).
- Developer dumps and test reports freeze the whole game, music included
  (mixer stopped, NDSP channel paused and resumed in place); the hang
  watchdog's 60 s deadline now counts from the dump's last progress step.
- Character shadows no longer darken their owner: animated characters mark
  their pixels in stencil bit 7 and the darkening pass skips them.
- Saves and settings are written by background threads with the same
  checked replacement (the console froze 0.3-2 s per save on the SD card).
- Opening: the bottom screen's rotating map with translucent clouds is
  prepared at most at 10 Hz when it is expensive (Azahar 25 %: 8.3 to
  15.6 FPS); the 3D image keeps its full rate.
- Lock FPS 60/Unlocked on the Old 3DS: a stale second-image cost no longer
  blocks interpolated images for the rest of the boot, and a miss waits at
  most 2 s (was 16 s).

## Local BUILD 138 — file-selector GPU hang on the Old 3DS, 2026-11-18

Azahar-checked; physical qualification pending.

- Fix the real cause of the white screen after Adventure: the console's GPU
  hung on the first file-selector image (BUILD 137 hang report: "la GPU no
  responde"), at every Lock FPS. The selector's wave grid was the only thing
  that ran the grey and toon branches of the lighting shader, which no
  console-run frame had ever used and Azahar draws without complaint. The
  grid is now lit on the CPU with the shader's exact arithmetic
  (`PreparedVertexLighting`) and drawn through the plain path; the image is
  byte-identical in Azahar.
- Add `tools/check_pica_hardware.py`: replays a captured GPU command list
  and reports any draw state never seen in command lists the console has
  executed (`dumps/prueba133`), plus hard-rule violations. Each player-journey
  step now saves its command list (`NN-paso-gpu/comandos_gpu.bin`).
- After a GPU hang the game no longer waits 250 ms per update for a render
  worker that cannot finish.

## Local BUILD 137 — Old 3DS hang, menu and music fixes, 2026-11-17

Azahar-checked (Old profile at 100 % and 25 % CPU clock); physical
qualification pending.

- Fix the Old 3DS freeze after Adventure (white fade on screen, music
  playing, HOME and POWER dead; open since BUILD 52). The file selector's
  update filled core 0, the fixed timeline never let the game thread sleep,
  and the render worker and libctru's APT thread (both below it) starved.
  The game threads now run below the APT thread (HOME/POWER always
  answered), and after three updates without an image the game waits for
  the render worker (bounded) and postpones its timeline, so an overloaded
  scene slows down instead of freezing. Reproduced and verified in Azahar at
  25 % clock: 0.2 FPS before, 8 FPS after.
- Bound every wait the HOME/sleep path can reach (render worker, GX queue
  via `--wrap=gxCmdQueueWait`, present fences, shutdown); a GPU that never
  answers is marked unusable instead of hanging the process.
- Add an always-on hang watchdog that writes a report, the unwritten log and
  the game's global memory to `sdmc:/3ds/sm64ds/cuelgues/`, and closes to the
  HOME Menu when HOME stays unanswered.
- Menu cost: buffer the log from the first game update (unbuffered SD writes
  cost 7–12 ms per line on the console), skip menu producers on updates
  whose image is never shown, and convert the file selector's wave grid once
  per point (bit-identical, 6x fewer conversions).
- Music: the core-1 mixer cap goes from 30 % to 50 % (`audio-cpu.txt`
  overrides it) and the NDSP queue from 3 to 4 buffers; a real underrun
  counter is logged.
- Add the player-journey test (L+SELECT+START): title, Adventure, new game,
  the whole opening and the walk to the castle door, with one report per
  step and the watchdog's hang count.

## Local BUILD 25–136 — native renderer test line, 2026-10-26 to 2026-11-15

Native-test CIAs (title `000400000F536500`); Azahar-checked, physical
qualification pending. Details: [current-work checklist](docs/CURRENT_WORK_CHECKLIST.md).

- Add a native Citro3D renderer that draws the game from an immutable scene
  capture: resident model banks, batched geometry, lighting, shadows, fog,
  dialogue text, menus and 2D layers, with a render worker per console model.
- Add Lock FPS 30 / 60 / Unlocked (Auto: 30 on Old, 60 on New); 60 and
  Unlocked interpolate between 30 Hz updates. On the Old 3DS the exact image
  is recorded while the GPU draws the interpolated one, and a budget guard
  falls back to one image per update when the pair does not fit.
- Cut CPU and GPU-command cost without changing the image (19 % fewer PICA
  words per image, 41–61 % fewer producer instructions, cheaper 2D map and
  sprite preparation, a non-blocking audio hand-off); `sin-recortes.txt` and
  `audio-espera.txt` switch the older paths back on for console A/B runs.
- Add the SELECT+START test mode: an unattended tour of all 50 levels with
  per-situation reports, screenshots and GPU/RAM dumps.
- Fix the upside-down Old 3DS HUD (since BUILD 114), missing character
  shadows, a hang in the first dump, Lock FPS not reaching the renderer and
  three 2D faults in the pinned PPU code.
- Document the renderer, its pipeline, the test mode and the Citro3D test
  oracles; remove orphaned laboratory sources and unused functions.
- BUILD 136 is BUILD 135 rebuilt from the published source after that
  cleanup; its image is unchanged.

## Local BUILD 24 — 2026-10-25

- Fix a first-boot crash when absent settings triggered C++ allocation before
  the original game heap was initialized. Backup and temporary paths now use
  bounded stack storage.
- Add an increasing BUILD label to title, pause, options and diagnostics, and
  carry it into build/package receipts. Failed reservations leave gaps.
- Add allocation-forbidden first-boot regression coverage. Host/ARM build and
  audit gates pass; directed Azahar Old/New checks cover first boot, level,
  pause and settings recovery. Physical qualification remains pending.

## Earlier local candidates — October 2026

Native-resolution/stereo fixes, direct companion access, level preloading,
console-specific budgets, interface layout, camera/movement corrections,
audio gain ramps, backup-first storage and diagnostic capture evolved across
local candidates. Exact artifact scope and limitations are preserved in
[STATUS.md](docs/STATUS.md), [the documentation index](docs/README.md) and
the dated investigation records. Those records are not public releases.
