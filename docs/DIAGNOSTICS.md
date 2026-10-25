# Diagnostics

Start a report with the visible BUILD number, console/model, CIA or 3DSX,
ROM revision and exact steps. Record what happened before the failure,
including level, camera direction, movement/display mode and stereo state.
For performance reports, include a short reproducible path and the frame
counter/subsystem trace context rather than only the lowest FPS number.

## Capture controls

| Shortcut | Action |
| --- | --- |
| L + R + A | Full diagnostic capture when enabled. |
| L + R + B | Partial capture without process RAM. |
| L + R + X | Delete diagnostic dumps; use deliberately. |

Developer settings control detailed overlays and full dumps. Captures are
written under `SD:/3ds/sm64ds/dumps/NNN-dump-YYYYMMDD-HHMMSS/` with sequence
metadata. A full capture can be large and pauses normal work while writing.
Do not remove power or the SD card during capture.

## What a capture means

Display data records the actual scanout framebuffer selection before dump
progress is painted, plus format/stride and relevant display registers.
This is different from merely reading the next render target. Process-memory
capture inventories readable mappings and includes VRAM and mapped DSP
windows where available. Render/trace companions describe the relevant
draw state and preceding subsystem timings.

These files are not an atomic snapshot of the entire console. GPU, DSP and
OS activity have independent timing; inaccessible memory is reported rather
than invented. Detailed formats, coverage and reader limitations are in
[NATIVE_RAM_DIAGNOSTICS.md](NATIVE_RAM_DIAGNOSTICS.md).

## Local analysis

```sh
python3 tools/inspect_luma_dump.py --help
python3 tools/inspect_integrated_vm.py --help
python3 tools/read_mapped_capture.py --help
```

Use the exact closed candidate's ELF. The Luma reader verifies the dump's
instruction window against the executable before symbol attribution; stack
words are candidate return addresses, not a guaranteed unwind. An ELF from
another BUILD can produce convincing but incorrect names.

BUILD 24's predecessor crashed during first boot when absent settings caused
an allocation through an uninitialized game heap. The fix and causal test
are recorded in [Status](STATUS.md). A similar-looking crash still requires
matching the actual BUILD and dump before drawing a conclusion.

## Sharing a report

Use a public issue for reproduction steps and sanitized text/captures only.
Full process dumps may contain ROM-derived data, paths, save state and
unrelated process contents. Keep them private. Do not upload ROMs, firmware,
saves or prepared gameplay archives. Review screenshots for personal data.
There is no automatic telemetry or diagnostic upload in these tools.
