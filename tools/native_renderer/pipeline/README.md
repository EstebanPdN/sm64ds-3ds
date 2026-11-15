# Native renderer build pipeline

These scripts build, run and compare the native-renderer game candidates
(`BUILD 86` onward). They operate on a generated checkout under the ignored
`build/native-renderer/` tree, which holds the owner's prepared game inputs
and never enters Git. Run every command from the project root.

The repository copies are the maintained versions. `build_playable.sh`,
`launch_playable.py`, `stop_playable.py` and `verify*.py` are copied into
`build/native-renderer/playable-r1/`, and `refresh_native_overlay.py` next to
the checkout's `source/`, because they keep their original `build/` paths.

## Evidence boundaries

- A successful build proves compilation and linking only.
- Azahar runs prove that a candidate boots, frames advance and screenshots
  match; they are not console performance or hardware validation.
- Run one Azahar instance at a time. `verify114.py` waits for any other
  instance and never closes one it did not start.
- Never reuse a BUILD number whose CIA was already handed out.

## One-time setup of a clean machine

1. Owner ROM: `build/roms/europe.nds` (SHA-1 `1367529f…`).
2. Python environments `.venv/integrated-native` and `build/python` with
   `ndspy==4.2.0 capstone==5.0.9 pyelftools==0.33 Pillow==12.3.0 numpy`.
3. devkitPro: export `DEVKITPRO` (default `/opt/devkitpro`) and `DEVKITARM`.
4. Movement reference: `SM64DS_MOTION_SOURCE` and `SM64DS_MOTION_ANIMS`
   pointing at a clean checkout of mkst/sm64-port `3f3c01ea…`.
5. Base tree:
   `.venv/integrated-native/bin/python tools/build_integrated_native.py --render60 --rom-only --jobs 8`
   (reserves a BUILD number; `build/integrated-native/render60-source` is the
   part the native checkout needs).
6. Cook the non-model catalogs and copy the model banks to
   `build/native-renderer/playable-r1/catalog-approx-r8`, then compose the
   headers with `compose_native_headers.py --out build/native-renderer/headers-r1 --model …/native_catalog.hpp`.
   [RECOVERY-2026-11-05.md](RECOVERY-2026-11-05.md) records the exact cook
   commands and their provenance.
7. Checkout: copy `render60-source` to
   `build/native-renderer/game-runtime-20261026-r1/source` (skip `.git`,
   `__pycache__`, `out`, `build3ds/{cmake,game,user-build,menu-compile}`), run
   `patch_native_test_entry.py` inside it, delete
   `setenv("SM64DS_PIPELINE","0",1);` from `port/platform/3ds/game_entry.cpp`,
   run `tools/prepare_integrated_native_renderer.py --source … --runtime`
   with the seven headers, copy `refresh_native_overlay.py` next to `source/`,
   and configure CMake as in the recovery record.
8. Copy `build_playable.sh`, `launch_playable.py`, `stop_playable.py` and
   `verify114.py` to `build/native-renderer/playable-r1/`.

## Build, check and package a candidate

```sh
export DEVKITPRO=/path/to/devkitpro
zsh build/native-renderer/playable-r1/build_playable.sh 136          # N above the reserved counter
tools/native_renderer/pipeline/run_tour_level.sh base 136 5 10 ""     # Bob-omb, 10 entrance situations
tools/native_renderer/pipeline/run_tour_level.sh lock60 136 5 10 "SM64DS_FPS_LOCK=2"
build/python/bin/python3 tools/native_renderer/pipeline/compare_screenshots.py run-base-sd run-lock60-sd
python3 tools/package_native_renderer_hardware.py 136 run-base-00     # CIA from a tested session
PLAYABLE_CIA=$PWD/build/native-renderer/playable-r1/hardware/build136/SM64DS-Native-BUILD136.cia \
  tools/native_renderer/pipeline/run_tour_level.sh cia 136 5 0 ""     # the installed CIA, 80 MB mode
build/python/bin/python3 tools/native_renderer/deliver_private_cia.py 136   # LAN QR for the owner's console
```

New `.cpp` files under `source/native_renderer/` or
`source/integrated_native/renderer/` are copied by `build_playable.sh`, but
must also be listed in the checkout's generated `port/CMakeLists.txt` (the
preparer does this for a fresh checkout).

## Scripts

| Script | Purpose |
| --- | --- |
| `build_playable.sh N` | Refresh the checkout from the repository and build `game-pN` (.3dsx and .elf). |
| `rebuild_candidate.sh N` | Rebuild an undelivered `game-pN` under the same number. |
| `compile_objects.sh OBJ…` | Refresh the checkout and cross-compile single objects (fast compile check). |
| `refresh_native_overlay.py` | Copy repository renderer sources into the checkout; refuses drift in recorded files. |
| `rehash_overlay.py` | Accept intentional edits inside the checkout in its overlay receipt. |
| `launch_playable.py`, `stop_playable.py` | Start and stop one isolated, interactive Azahar session. |
| `verify114.py` | Drive the SELECT+START test tour, one emulator boot per level, carrying the SD results. |
| `verify110.py` | First-boot smoke run with the recorded BUILD 107 input recipe. |
| `run_tour_level.sh` | One tour level with env.txt variables and SD switch files. |
| `compare_screenshots.py` | Screenshot-by-screenshot equality of two tour sessions. |
| `compare_gpu_state.py` | GPU register and uniform state per draw of two command-list dumps. |
| `patch_native_test_entry.py` | Add the native-test CIA entry (own title, save and log) to a fresh checkout. |
| `compose_native_headers.py` | Compose the seven catalog headers from cooked catalog folders. |

Test-mode reports, SD switch files and report fields are described in
[docs/TEST_MODE.md](../../../docs/TEST_MODE.md).
