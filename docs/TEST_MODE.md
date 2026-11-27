# Automatic test mode

The native-renderer test build (native-test CIA, BUILD 110 and later) carries
an automatic test mode. It plays a fixed script in every level and writes a
report per situation to the SD card, so a console run can be analysed on a
computer afterwards. The dated, per-build record of how it evolved is kept
in Spanish in [MODO_PRUEBA.md](MODO_PRUEBA.md).

Test-mode figures from Azahar only show that a build boots, advances and
draws the expected image. Frame times count only from a physical console.

## Starting a run

| Chord | Run |
| --- | --- |
| SELECT+START | Tour of all 50 levels, each from a fresh boot. |
| R+SELECT+START, inside a level | The same entrance script at the current spot. |
| L+SELECT+START (BUILD 137) | Player journey: the game played from the title on a new save. |

1. Choose **Lock FPS** in the pause menu first (30, 60 or Unlocked; Auto is
   30 on an Old 3DS and 60 on a New 3DS). The tour keeps that choice.
2. Press the chord. The game shows `REINICIANDO`, exits and the HOME Menu
   reopens it (in Azahar, reopen it by hand).
3. Do not touch the controls. The top strip shows the level and the number
   of reports. START cancels the tour.
4. The lid can be closed: the console does not sleep during a run. At the end
   the notification LED turns green (red if the tour cannot continue) and the
   lower screen shows the totals. START returns to normal play.

Each level: a fresh boot, the game's own `StartFile`, a rotating character
(Mario, Luigi, Wario, Yoshi), 24 entrance situations (standing, camera turns,
walking, running, jumps, attacks, pause menu, crouching, crawling and more),
then up to four zones beside live actors with three situations each. A
death repeats the action; leaving the level restarts it up to three times.
The run plays on its own save file (`prueba/partida.sav`); the owner's save
is never read or written. About 1.5–2 minutes per level on a console.

## Player journey (BUILD 137)

L+SELECT+START plays the game like a person, on its own new save
(`prueba/partida.sav`): it touches TOUCH TO START, Adventure and File A,
watches the whole opening (a text is advanced with A only while it waits for
A), reads Lakitu's text, walks Yoshi across the castle grounds with
camera-independent closed-loop steering (the game maps the Circle Pad to the
world through the camera with a reflection) and pushes the castle door. A new
game finds that door locked (the key is with the garden rabbit, not chased
yet), which ends phase 1. One report per step (9) goes to
`prueba/NN/jugador/`, and `resultado.txt` ends with the verdict, the texts
advanced, the stuck-jumps, the route point reached and the number of hangs
the watchdog detected. Logic and tests: `test_mode_journey.hpp`,
`tests/native_renderer/test_mode_journey.cpp`. Azahar (Old, 100 % clock):
complete in 164 s, 9/9 reports, no hang.

## Hang watchdog (BUILD 137, always on)

A thread at priority 0x2c on the application core checks every 250 ms that
game updates, finished images and `aptMainLoop` entries keep coming. When the
picture stops for 6 s while the game runs, the game stops for 15 s,
`aptMainLoop` is not reached for 15 s, a pending HOME/POWER waits 5 s, the
GPU completes nothing for 2 s, or a test-mode report or dump runs past 60 s,
it writes
`sdmc:/3ds/sm64ds/cuelgues/cuelgue-NN.txt` (reason, scene, menu state, every
thread's last stage and its age, counters, audio and GPU state, the last 64
breadcrumbs), `cuelgue-NN-log.txt` (the unwritten part of the log buffer) and
`cuelgue-NN-memoria.bin` (the game's .data/.bss block, at most 32 MB; the
emergency-exit report omits it), and lights the LED
red. It writes through FS:USER only and formats integers only, so it never
takes a stdio, newlib or heap lock a stuck thread may hold. When HOME or
POWER stays unanswered for 20 s it asks APT to close the application from a
helper thread created at start, then calls `svcExitProcess` after 1.5 s, so
the HOME Menu returns (`cuelgue-sin-salida.txt` disables that; unverified on
hardware, Azahar does not deliver HOME). A poll gap above 2 s (sleep mode or
a suspended process) resets the clocks instead of reporting. GX waits are
bounded by `--wrap=gxCmdQueueWait`: it waits in 100 ms slices and gives up
only after 20 slices in which the queue's `curEntry`/`lastEntry` did not
move, so a long but progressing queue is never cut. The GPU is then marked
unusable (report reason `la GPU no responde`) and the backend stops issuing
GX work and skips captures; the picture freezes but HOME keeps working.
Development faults (.3dsx `env.txt`): `SM64DS_DEV_FAULT=hang-render:N` and
`hang-game:N`.

## What is written to the SD card

Everything goes to `sdmc:/3ds/sm64ds/prueba/NN/`:

| File | Content |
| --- | --- |
| `resultado.txt` | Header (BUILD, Old/New, Lock FPS), one line per report and one per level, then the `RECORRIDO` totals. |
| `LL-level/SS-situation.resumen.txt` | Frame-time percentiles; game thread (update, 2D preparation, 3D producers); render thread (work, draw stages, GPU, uploads); GPU work per image; memory; updates without an image; render health; game state; producer stages; models not drawn. |
| `LL-level/SS-situation.cuadros.csv` | One row per presented image (ticks at 268,111,856 Hz) with per-stage costs. |
| `LL-level/SS-situation.arriba.bmp`, `.abajo.bmp` | Both screens at the moment of the report. |
| `LL-level/SS-situation.faltan.csv` | Only when a model was not drawn: which and why. |
| `LL-level/volcado/`, `volcado-zonaN/` | Deep dump after the first report of the level and of each zone. |
| `LL-level/registro_cuelgue.txt` | Only if the level did not finish: the log of the failed boot. |

A dump holds the PICA200 command list of one image (`comandos_gpu.bin`,
`dibujos.csv`), the scene the renderer drew (`escena.json`), its textures,
the DS-side video state (`ds_video.bin`), the CPU 2D planes (`planos_2d.bin`)
and every live actor. The whole process memory (`memoria.vm`, about 80 MB)
is saved in the first level, or in every level when the empty file
`prueba/ram.txt` exists. A full tour writes about 600 MB.

### Report fields added for the 60 FPS work (BUILD 134)

- Header `limite 30 FPS`, `limite 60 FPS` or `limite sin FPS` (Unlocked).
- Block `== interpolacion ==`, since the previous report:
  `intermedias` (interpolated images shown), `exactas_en_paralelo_con_gpu`
  (Old 3DS exact images built while the GPU drew the interpolated one),
  `presupuesto_cabe` / `presupuesto_no_cabe` (updates where the pair fitted
  in 92 % of the period, or showed one image), `segunda_imagen_perdida`,
  `costo_imagen_ms` and `costo_segunda_ms` (measured CPU per image).
- Audio: `juego_espera_tras_imagen_ms` and `lotes_aplazados` (command
  batches delivered after the image went to the render thread).

With Lock FPS 60 on an Old 3DS, `intervalo_ms` percentiles come from the CPU
end of each image, not from the LCD, and can read 5–7 / 28–30 ms while the
display is even. Judge such runs by `fps`, `sobre_34ms` and the interpolation
block.

## SD switch files

Empty files in `sdmc:/3ds/sm64ds/`, read once at start-up:

| File | Effect |
| --- | --- |
| `sin-recortes.txt` | The BUILD 133 paths of every exact cost cut (same image, slower), to measure the cuts on the same console. |
| `audio-espera.txt` | The game thread waits for the audio mixer again. |
| `prueba/ram.txt` | Save the whole process memory in every level. |

Development builds (.3dsx only; a CIA never reads `env.txt`) also accept
`SM64DS_FPS_LOCK=0|1|2` (Unlocked/30/60), `SM64DS_FPS_LOCK_CYCLE=N` (30 → 60
→ Unlocked every N updates), `SM64DS_PRUEBA_PASOS=N` (entrance situations per
level) and `SM64DS_PRUEBA_ZONAS=N` (zones per level).

## Reading the results

Copy `3ds/sm64ds/prueba/` from the SD card and run:

```sh
python3 tools/test_mode_report.py /path/to/prueba --markdown report.md
python3 tools/test_mode_report.py /path/to/prueba --detalle            # one row per situation
python3 tools/test_mode_dump.py /path/to/prueba                        # summary of every dump
python3 tools/test_mode_dump.py /path/to/prueba/01/06-bob-omb/volcado --dibujos --comandos list.txt
python3 tools/test_mode_sheet.py /path/to/prueba sheets/               # one contact sheet per level
python3 tools/native_renderer/pipeline/compare_gpu_state.py DUMP_A DUMP_B
```

`compare_gpu_state.py` replays two dumps of the same frame and reports
whether the GPU state of every draw is identical (for example with and
without `sin-recortes.txt`). Process memory is read with
`tools/inspect_integrated_vm.py` and the ELF of the same BUILD.

## Implementation

- `source/integrated_native/renderer/test_mode_script.hpp`: script, level
  table, synthetic pad, state file and run state machine (host test
  `native_test_mode_script`).
- `test_mode.cpp` and `test_mode_game.cpp`: SD state, chord, per-level
  restart, crash records, zones and on-screen status.
- `game_runtime.cpp`: input wrappers, the reports and the dumps.
- In Azahar, `tools/native_renderer/pipeline/verify114.py` chains the boots of
  a tour and `run_tour_level.sh` runs one level.
