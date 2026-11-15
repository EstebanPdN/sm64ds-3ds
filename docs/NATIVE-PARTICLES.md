# Native particle images and billboard capture

This is incremental work, **not an accepted complete game renderer**. The
comparison route remains the default. The fresh runtime captures the observed
title models, sprites and billboards without interpreting graphics commands;
its compositor still refuses the complete scene. Physical-console FPS is unmeasured.

## Resources and lifetime

The original CPU simulation, active lists, allocation and behaviour callbacks
remain authoritative. Preparation profiles pin the original loader/producer
sources and the supported EU revision0 ROM. The host prepares two image banks:

| ID | Original source | Definitions | Images |
| --- | --- | ---: | ---: |
| 0 | Embedded `data_02075f14` | 321 | 46 |
| 1 | `data/DSMT/mdst_kira1_spa.bin`, FAT214, handle658 | 1 | 2 |

These 48 images contain 23,040 texels and require 92,160 GPU payload bytes when
all are resident. Only requested images load. This is not whole-game RAM/VRAM
accounting. Conversion and tiling happen on the host. Indexed images retain
palette-zero RGB plus a TEV-selectable alpha mask; independent alpha is retained.
Sharing the host pixel decoder leaves all 696 existing model catalog files byte-identical.

Source pixels/records may disappear after original upload. Manager associations
retain compiled identities. Source-file release does not retire the manager;
constructor reuse, menu cleanup and the core tracker destructor do. Descriptor
identity comes from the active manager's own table, never virtual VRAM addresses.

## Native image-only format

`ImageBank` borrows immutable `SM64NIM1` bytes without allocation. Little-endian
header (128 bytes): magic8; version 1 at 8; exact size at 12; image count 1–128 at 16; table
offset 128 at 20; GPU payload sum at 24; whole-file CRC32 at 28, zeroed during its
calculation; original source SHA256 at 32; reserved zero bytes 64–127.

Each 32-byte record has offset/bytes/width/height/alpha-class and three zero
words. The table ends at a zero-padded 128-byte boundary, followed by canonical
image payloads with no overlaps or trailing data. Pixels use bottom-row-first
Morton-tiled ABGR and native top-left UVs. Dimensions are powers of two from 8–1024.
Disk bound 64MiB; scene transient-bank bound 8MiB; GPU budget shared with models.
A failed open preserves the caller's previous borrow.

Image IDs and model IDs have separate namespaces. `NativeImages` extends a
working set by uploading only missing images. Failed extensions roll back new
textures, preserving old handles. All requests are pinned before cross-namespace
LRU eviction. Warm frames perform no bank reads, texture uploads or resource
creation. Bank timing includes I/O, validation and upload, not isolated storage latency.

## Producers

Primary `0204be40` and child `0204bbd8` are native typed producers. Four other
particle shapes remain guarded migration work. Original dispatch/list traversal
continues. Capture preserves counter mutation before alpha 0 return, including
count 63 wrapping to 0 before limit comparison; fixed-point scale/aspect rounding;
sine/cosine lookup; primary/child repeats; signed16 UV wrapping; RGB5 colour;
five-bit opacity; sampler wrap/mirror; and palette-zero alpha selection.

The original centroid helper runs once. GPU uniforms transform local vertices
from the captured view-space pose; global view is not applied twice. Published
frames own vertices/state/IDs, with no source pointers. `020190b8` preserves its
condition and `data_0209d464` bookkeeping; native frame ownership replaces its
legacy swap. Optional group timing includes traversal, binding and capture.
Distinct poses still prevent batching; instance/palette batching needs profiling.
Fewer counted legacy writes do not establish a performance improvement.

## Validation and remaining work

- 70 host CTests pass, with portable C++ ASan/UBSan.
- All 48 authentic images match the unchanged reference decoder, including both
  indexed palette-zero policies.
- 2,048 authored 32-bit ARM primary/child cases match original pose matrices,
  counters, opacity and repeat fields. Lifetime, alpha 0, flush and inactive-route
  contracts pass. Checked native capture intervals write no graphics registers.
- Six final Old/New Azahar lab runs: 660 regular frames, 22 target captures,
  1,421,692 analytically checked pixels, zero mismatches. Additional whole-image
  short/general shader comparisons match exactly.
- Full-game Old/New probes: 180 updates each, no unmapped access or observed
  producer failure. At frame 121: two sprite quads, twelve particle quads and two
  lit model parts. Counted legacy graphics writes are 0 at sampled frames 1/121.
  All 180 complete original native scenes remain refused.

Reference frame 121 routes title 3D to the lower screen; upper output is black
at that point. Native routing, background/2D, brightness, transparent clear and
polygon-ID/fog/depth policy remain necessary for original-menu acceptance.
Target readbacks are not physical LCD screenshots. Tracked capture workspace
916,600 bytes is 17,928 above the sprite checkpoint, not complete app memory.

Private evidence: `build/native-renderer/PARTICLE-REPORT.json` and its frozen
candidate. No commits, push, releases or physical-performance claims.

## Local preparation

Run `tools/native_renderer/particles.py --rom build/roms/europe.nds --output
build/native-renderer/catalog-particles-NEW` using the project virtualenv.
Pass its `native_image_catalog.hpp` through `--image-catalog` to
`tools/prepare_integrated_native_renderer.py --runtime`, alongside the model
`--catalog`, in a newly generated checkout under `build/`.

`tests/native_renderer/run_particle_arm.py --output build/native-renderer/particle-arm-NEW`
uses project devkitARM, Unicorn and pyelftools. New output directories preserve
earlier evidence. ROMs, prepared pixels and captures remain local under ignored `build/`.
