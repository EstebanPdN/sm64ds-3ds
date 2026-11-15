# Testing and evidence

Choose checks by the affected contract. A successful host test, ARM object,
linked package, emulator scenario and physical console run establish
different things. Record the candidate hash, visible BUILD number, model,
inputs and limitations whenever reporting runtime results.

## Portable source checks

```sh
python3 scripts/check-repository.py
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Debug \
  -DSM64DS_ENABLE_SANITIZERS=ON
cmake --build build/host --parallel 4
ctest --test-dir build/host --output-on-failure
python3 tests/test_build_tools.py
python3 tools/setup_dependencies.py --backend-only
python3 tests/integrated_audio_save/run.py
```

These use authored/synthetic inputs or temporary files. The audio/storage
runner also consumes public decompilation headers, fetched by the explicit
preparation step above. They do not require
the game ROM, extracted resources, saves or an emulator. CTest covers shared
parsers/math and the registered integration helpers; `assert` remains active
even in Release. See [tests/README.md](../tests/README.md) for suite ownership.

The repository check validates staged/tracked placement, source pins,
maintained-document destinations and private-output exclusions. It is a
bounded publication check, not a complete secret-scanning guarantee.

## ARM and resource-backed gates

Source-preparation contract suites additionally need the pinned public
backend decompilation, prepared by `tools/setup_dependencies.py --backend-only`:

```sh
python3 tests/options_menu/test_prepare_contracts.py
python3 tests/options_menu/test_native_pause_hooks.py
```

```sh
build/python/bin/python tests/integrated_audio_save/run.py --arm
build/python/bin/python tests/movement_lab/run_native_bridge.py
build/python/bin/python tools/movement_lab/build_core.py --arm
```

Use the SDK and private motion animation inputs documented in
[Building](BUILDING.md). The integrated candidate builder also runs required
controls, material/uniform/vertex, PPU, archive, preparation and executable
audits. Some legacy runners require private ROM data or historical generated
objects; their filenames alone do not make them portable tests.

Do not skip a required gate by fabricating its receipt. Retain whether a
graphics comparison used an authored fixture or an actual captured state.

## Native renderer checks

The native renderer's host suites are part of the CTest run above (`native_*`).
The exact cost cuts of BUILD 134–135 are each checked against a reference
copy of the code they replace:

| Check | Compares |
| --- | --- |
| `native_pica_state` | Register-delta writes against the unmodified Citro3D 1.7.1 `effect.c` and `buffers.c` over a PICA register model. |
| `native_uniform_residency`, `native_sdk_uniforms_*` | Uniform residency against the upstream `uniforms.c`. |
| `native_render_cuts` | Lighting cache, batch plans, skin upload and quad domains against the copying reference paths. |
| `native_producer_cuts` | Material, pose, part and skin producers against verbatim copies of the BUILD 133 routines. |
| `native_test_mode_script` | Test-mode script, zones, re-entries and state file. |

Further gates need the local tools or checkout:

```sh
python3 tests/native_renderer/test_ppu_cost_cuts.py      # generated PPU vs BUILD 133 (needs the local checkout)
DEVKITARM=/path/to/devkitARM python3 tools/test_native_producer_cuts_arm.py   # console compiler in Unicorn, six FPSCR modes
DEVKITARM=/path/to/devkitARM python3 tools/test_native_renderer_math_arm.py
```

In the emulator, the [native pipeline](../tools/native_renderer/pipeline/README.md)
runs one test-mode level per command and compares two runs screenshot by
screenshot (`compare_screenshots.py`) or draw by draw (`compare_gpu_state.py`).
The tour drives the same inputs every time, so an unchanged renderer gives
identical screenshots; a difference is investigated, never accepted.

## Emulator scenarios

Use a unique isolated Azahar profile and its own SD copy; preserve personal
settings, NAND, saves and ordinary emulator profiles. Identify Old/New
memory modes from the loaded executable, not only a window label. Record
the exact package/CXI and expected scenario before starting.

Before a general test release, cover first boot with missing settings,
existing settings and backup-only settings; the star introduction, title,
file selector, loading, play, pause, options and save/reopen. Then exercise
camera sweeps, movement modes, water/damage/script handoffs, stereo toggles,
different levels and sleep/resume/exit. A directed level fixture is useful
for isolation but does not replace normal progression.

Capture the real rendered displays for UI changes. Measure subsystem time
and frame distributions for performance work, distinguish intentional waits
from CPU/GPU cost, and correlate a hitch capture with the relevant frame.
Avoid inferring a cause from the total FPS counter alone.

## Current evidence and limits

BUILD 24 passed its complete build/audit gates and directed Azahar Old/New
first-boot, level and pause checks. The original missing-settings crash was
symbolized against an exact code window and reproduced; the new allocation-
forbidden production-path regression fails with the old helper and passes
with the fixed one. Settings save and backup recovery were checked in
isolated profiles. The normal Old title-to-file flow was also observed.

Native-renderer BUILD 135 and 136 passed the host suites and the gates
above (136 also from a clean checkout, as CI runs them); in
Azahar (Old and New profiles, .3dsx and the 80 MiB CIA) its screenshots match
BUILD 133 with Lock FPS 30, 60 and Unlocked, with the older paths switched
back on, and while Lock FPS changes during play. Console timing is measured
only from the [test mode](TEST_MODE.md) on a physical system.

The [status journal](STATUS.md) records candidate-specific details. These
checks do not establish completion, sustained FPS, audible quality,
power-loss durability or physical Old/New acceptance. Publication-oriented
host checks run on current source without claiming a new game candidate.

## CI scope

GitHub Actions runs repository integrity and portable host regressions with
read-only contents permissions. It does not fetch game data, use a developer
NAND, compile a playable package, sign artifacts or create releases.
