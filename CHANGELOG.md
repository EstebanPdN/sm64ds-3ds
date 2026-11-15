# Changelog

All entries below are **unreleased**. BUILD numbers identify local test
candidates, not GitHub releases or a promise of full-game compatibility.

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

## Local BUILD 25–135 — native renderer test line, 2026-10-26 to 2026-11-15

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
