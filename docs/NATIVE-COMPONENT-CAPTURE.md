# Native component diagnostics

The original game's native producers can export one **owned native component
snapshot**, including prepared 2D placements. This is diagnostic input for the standalone PICA lab.
It cannot certify a complete original scene, either LCD, physical hardware,
RAM/VRAM contents or performance. The game's complete-scene rejection remains
unchanged, including when the component is successfully captured or replayed.

Set `SM64DS_FRESH_NATIVE=1` and
`SM64DS_FRESH_NATIVE_COMPONENT_CAPTURE=121` before startup to capture native
presentation 121. The option is parsed once; absent/invalid options disable it.
The allowed frame range is 1–1,000,000. Normal frames allocate nothing and do
no capture IO. The selected frame incurs validation and file IO: its timing is
intrusive and must be excluded from performance comparisons.

The private output is
`sdmc:/3ds/sm64ds/native-component-<tick>-<frame>.json`. Its format is
`SM64_NATIVE_COMPONENT_SCENE_5`, with explicit scope and false full-scene and
physical-capture assertions. Producers must succeed and their native capture
must be valid. The runtime borrows its own arrays before a refused frame clears
their used counts; it does not acquire or publish an unsealed frame.

The snapshot retains ordered occurrences, logical bank/image identities,
native material and matrix values, local quad vertices, image layers, the
selected LCD/3D order, light sets and shared
lighting states. Original semantic polygon IDs/fog remain visible even though
their native pixel rules are unfinished. It includes clear, missing-2D and
legacy-writer annotations. It contains no source pointers, GPU handles,
graphics commands, original texture bytes or arbitrary memory pages.

Validation covers the complete input before writing the first byte, including
ordered ranges, bounds, all serialized floats and lighting references. The
writer uses the application's default C numeric locale. The caller publishes
only after temp-file write, flush and close succeed, followed by rename. A
failed IO removes the temporary file; nothing is reported as captured.

Private replay preparation:

```sh
python3 tools/native_renderer/component_fixture.py \
  build/private-component.json build/private/native_component_fixture.h
```

Configure the existing SDK laboratory build with
`SM64DS_NATIVE_COMPONENT_FIXTURE` pointing to that header and both prepared
model/image catalogs, then build `sm64ds-native-component-lab_3dsx`. The generated
header preserves captured values; no source graphics decoding runs in the lab.
The input and header remain private under `build/`. The lab needs the matching
private `.nmd` and `.nim` banks in `sdmc:/sm64ds/native`.

Version 5 adds ordered skin/procedural mesh occurrences and their owned normal
vertices, highlight flags, toon/grey lighting modes and the toon palette. It
fixes the previous refusal of snapshots containing skin. The fixture preserves
screen-layer vertex ranges as well. A compiled JSON → native data → JSON
round trip checks every authored field. Capture IO remains intrusive and must
not run inside a measured interval.

Version 4 adds each layer's exclusive physical-LCD `clip` bounds; all zero
means unclipped. Version 3 introduced the native resource namespace (`source`:
images=0, canvas=1) and prepared mesh `part`. The fixture tool also accepts historical
version-2 image layers (images/part zero) and version-1 captures, which have no
native image layers and default to upper-LCD presentation. Canvas layers need
their matching private catalog and `.nmd` assets; equal numeric IDs across
model, image and canvas catalogs identify independent resources.

The lab first verifies the captured original scene stays refused. It then
explicitly substitutes an opaque black background and ordinary native polygon
policy and omits missing 2D. Captures with native image layers preserve their
captured camera, LCD and layer order, composing actual native 3D/2D together.
Historical captures without image layers display 3D on each LCD in turn.
Every substituted ID/fog value is logged. This preview **is not original-menu
acceptance**. It is useful for testing positions, shading and resource
residency before complete pixel/composition rules are integrated.

Use both the guest log and independently inspected images. A fixed replay
checks first/last and Old/New image equality, and warm frames check that static
resources are not recreated/reuploaded. Queue times are emulator guest values.
The scene owner now renders to a 256×512 RGBA8/depth surface (1,048,576 bytes),
then samples the selected 240×400/320 viewport into the paired RGB8 compositor
outputs (518,400 bytes). Backend counters include that surface; LCD targets are
reported separately. SDK overhead, gfx buffers and other application memory
remain excluded. This pipeline uses 798,976 more VRAM payload bytes than the
prior direct 768,000-byte target, plus a 256-byte resident composition mesh and
one composition draw. It enables 2D integration; it is not a performance win
by itself. Physical timing and residency tuning remain required.

```sh
python3 tools/native_renderer/check_component_capture.py \
  --session build/private-component-session
```

The independent checker verifies six exports, source checksums, unchanged fixed
frames, black inactive LCDs and identical 4:3 content in both LCD coordinate
systems. `--direct-session` additionally compares the top result with the
previous direct-target native component. Neither is the original game's
complete visual oracle. A real JSON → generated C++17 → sanitized JSON
round trip is retained with the private evidence.

Captured GPU buffers are fully read and checksummed before export, with the
host independently verifying that checksum. A checksum-free export reproduced
corruption in Azahar while the complete-read export showed the expected star.
The cause remains unresolved; the checksum alone does not establish hardware
cache correctness. Retain the failed acquisition and preimage binary. Never
interpret a successful draw counter as proof that an exported image is valid.

The initial frame-121 Old/New semantic snapshots matched byte for byte: two
model parts, two sprites and twelve particles. The component still lacks the
original menu text/2D composition. Physical Old/New performance and complete
progression remain pending.
