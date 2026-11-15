# Native prepared backgrounds

The renderer can compose prepared, sparse 2D geometry through Citro3D on both
LCDs. It does not interpret tilemaps, DS graphics commands or emulated VRAM.
The original game's background and font producers are still being connected;
**no complete original native game frame is accepted yet**.

## Preparation and ownership

`tools/native_renderer/source_members.py` resolves reviewed menu resources
through the original filename-pointer table and the counted NARC archive
ranges. Large logical handles identify archive members, rather than direct
filesystem IDs. Explicit LZ10 decoding happens offline only.

`backgrounds.py` validates the counted NCSC screen, character and palette
association against `background_profiles.json`. It omits transparent tiles,
bakes flips and palette choices into unique atlas tiles and emits normalized
native geometry in the existing CRC-checked NMD container. Original RGB5
expansion and the reviewed nearest-sampling bias are baked offline. No source
tile words, pixel conversion or per-vertex CPU transform run during rendering.

```sh
.venv/integrated-native/bin/python tools/native_renderer/backgrounds.py \
  --rom /private/owner.nds --output build/private-backgrounds --preview
```

The generated `native_canvas_catalog.hpp` pins the logical canvas identity,
source SHA256, image count and part count. Pass it through `--canvas-catalog`
to the game preparer, or `SM64DS_NATIVE_CANVAS_CATALOG_HEADER` to CMake.
The files remain private under `build/`.

Frame-owned `ImageLayer` records select the canvas namespace, bank, part,
physical LCD, order and rectangle. They contain no borrowed GPU handles or
source pointers. `SceneRenderer` pins all needed assets before eviction,
shares its memory budget across model/image/canvas residents and resolves
mesh/material spans only on the render thread. Canvas loading validates flat
normalized geometry, unlit alpha materials and the prepared catalog identity.
Warm frames reuse resident meshes and atlases. Unknown identities and invalid
parts refuse the paired output; image IDs never fall back to the canvas cache.

## Current evidence and costs

The first prepared resource is MAIN BG3 resource 1, using English characters
and its reviewed palette. Its artwork contains the SOUND/BACKLIGHT options
labels. Its 32 visible tiles become a 64×32 RGBA8 atlas and one indexed mesh:

| Payload | Bytes |
| --- | ---: |
| Atlas | 8,192 |
| Vertex/index allocation | 3,456 |
| Combined prepared GPU payload | 11,648 |
| Full 256×256 RGBA8 bitmap alternative | 262,144 |

This is about 22.5 times smaller than that bitmap alternative. It is not a
measured speedup over the original renderer. The original already avoids
unchanged tilemap transfers.

`background_reference.cpp` links the unchanged provider PPU into a host-only
oracle, sets the reviewed characters/palette/map and exports original RGB.
The prepared output matches that reference exactly. Old/New isolated Azahar
runs then verify the native PICA output at 1.25× scale on both LCDs, fixed-frame
stability, resource reuse and independent image/canvas namespaces. An authored
blue image deliberately shares canvas bank ID 5001. The checker distinguishes
those authored pixels from original PPU reference coverage.

The namespace probe uses four draws, 132 triangles and 12,416 bytes of owned
linear GPU payload, including the blue image and compositor geometry. Both
LCDs share one canvas upload; warm mesh/image uploads and resource creates are
zero. With native clip metadata, CPU composition storage is 544 bytes (formerly
512). The current scene owner still
allocates a 1,048,576-byte 3D render surface even for this empty-3D laboratory;
LCD outputs use another 518,400 bytes. Avoiding the unused surface is a pending
optimization. These counters exclude SDK, allocator, gfx and other game memory.

## Native crop and source integration

The immutable prepared map cannot simply be drawn at its captured scroll.
Original `020c0638` first crops/translates the source map in whole tiles and
stores only a sub-tile remainder in the scroll fields. For example, startup
translates this map by (0, -192), hiding its labels while recorded scroll is
(0, 0). Original `020c0288` also edits palette selections in the source map.
Both operations now have a typed source adapter for the reviewed resource 1,
English layout 1 and transparent fill. Resources 6/7 are mutable text surfaces and need original
glyph, clear, copy and overlap semantics; a frozen text bitmap cannot replace
them.

`canvas_windows` now provides the native finite-domain crop/repeat calculation.
It returns up to four full-mesh origins with exclusive integer viewport clips,
uses no allocation, pixel conversion or geometry copies, and succeeds with no
windows for completely cropped sources. Invalid inputs preserve the caller's
previous result. Arbitrarily large scroll/translation inputs are bounded before
origin arithmetic. A source adapter must supply the **whole-tile translation**
and the complete integer scroll separately, and prove transparent fill; this
API does not decode source tile words.

Its host oracle exhausts every pixel in 4,096 generated small-domain cases and
additional signed-limit/seam cases. A separate ARM differential executes the
unchanged original `020c0638` for 4,096 cases and checks 196,608 viewport probes
against its actual translated maps, including negative shifts, sub-tile
remainders and additional scrolling. Power-of-two domains use a mask instead
of signed remainder: the ARM probe records zero native division calls for
those cases; non-power-of-two domains retain the tested general path. This is
an operation-count result, not a physical timing claim.

`canvas_placement` maps the resulting windows into physical LCD rectangles and
exclusive `ScreenClip` bounds. The compositor applies Citro3D scissoring with
portrait target coordinates, restores it before unclipped layers/whole-screen
tone, and validates both LCDs before publication. Image/canvas layers retain
their clips in owned version-4 component snapshots. Equal clips avoid repeated
scissor changes. Invalid lower-LCD bounds refuse the pair and later valid
frames recover.

An exact pixel-centre seam initially selected the preceding tile in rotated
rasterization, producing 86 wrong pixels on each LCD in one case. A 1/16
physical-pixel origin bias fixes the tile choice without changing resident
geometry or prepared UVs. The corrected ten-case PICA lab covers positive and
negative shifts, startup hiding, wrapping, additional scrolling and both-axis
pixel-centre ties. Its original oracle executes the actual ARM map transform,
then the unchanged host PPU. Old/New captures match all 3,456,000 checked
physical output pixels with zero channel error. The separate blue-image probe
also verifies that an unclipped layer after a clipped layer retains all pixels.

The crop lab retains 12,160 bytes of owned linear GPU payload and uses three
to nine draws for the paired LCDs, including the empty 3D composition draw.
CPU composition capacity grows once from 544 to 2,176 bytes for eight image
windows plus the 3D layer. Warm mesh/image uploads and creates remain zero.
The combined real 3D/caption replay remains at 18 draws, 104 triangles and
92,416 GPU linear bytes; clip metadata changes its CPU composition storage
from 256 to 272 bytes. The game runtime workspace is now 926,560 bytes, 512
more than the first canvas checkpoint. None of these figures is total RAM.

`menu_backgrounds` registers the source owner/map identity once, validates its
reviewed source CRCs, retains only native translation/variant values in frame
commands and retires the association with the original owner. For supported
`020c0638` calls it preserves dirty/scroll/coordinate CPU state while omitting
the destination fill/copy. Unknown identities, nontransparent fill and mutation
contracts retain the original CPU helper and refuse native coverage. Publication
revalidates the owner before emission; fully hidden windows load no GPU bank.

The adapter preserves the original `020c0288` writes and recognizes the exact
six-call palette batch inside `020b1390`; arbitrary edits invalidate coverage.
`backgrounds.py --variants` prepares five maps/parts using four unique meshes,
one 16,384-byte atlas and 13,824 bytes of geometry (30,208 bytes total). The
independent oracle executes original ARM selection/palette code before the
unchanged host PPU. A separate ARM bridge checks 2,048 adapter crops, six actual
selection calls, lifetime handling, inactive forwarding and refusal paths.

The real startup probe replaces one crop: 2,048 fill bytes and 512 copy bytes
are avoided while the labels are hidden at translation (0,-192). A later title
setup repeats that operation. These are startup totals, not savings per frame
or a measured FPS improvement. Original font producers remain unmigrated and
complete scenes remain refused.

The maintained port intercepts the original wrench to open its own native
Options menu. The prepared original sound/backlight selection variants are
therefore not that menu's active UI route. See [Native interface](NATIVE-INTERFACE.md)
for its publication/texture consumer. Preparing the original variants does not
claim coverage of the maintained Options menu or of resources 6/7.

Enabled layers, windows, backdrop, masked effects, transparent 3D composition
and original polygon/fog/depth policy remain necessary for complete scenes.
The standalone lab uses an explicit black backdrop. Its captures are completed
GPU-target exports, not physical LCD photographs or full RAM/VRAM dumps.
Emulator guest ticks are diagnostic; physical Old/New timing and sustained
performance remain unverified.
