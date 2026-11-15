# Original menu 2D migration

The first menu caption is captured from the live game into a frame-owned native
image layer and composed with its native 3D component through Citro3D.
**No complete original game frame is accepted yet.** Original 2D backgrounds,
fades, masks, other resources and pixel-policy interactions remain to be
migrated. The complete-scene gate stays shut.

## Active migration after BUILD 42 (2026-10-29)

BUILD43 integrates the ordinary Message entrypoint (`0201f32c`, font mode0):
the parser/input/game timing remain original native code, while AddChar and
line/scroll commits retain character identity, advance and ink instead of
painting/moving DS font tiles. `DialogueText` publishes one owned atlas batch
per physical text screen. The bridge compositor accepts these native screen
batches; this is deliberately a migration stage, not bridge removal. Source
font loading, window effects, cursors and other Message entrypoints remain.

BUILD44 fixes glyph-edge coverage missed by the regular font-grid lab. At
scale5/4, source x=2mod4 maps to an LCD pixel centre. PICA's rotated fill rule
can omit the leading column or include a trailing one. A1/16physical-pixel
upper-left shift selects the intended glyph domain, matching the existing
native canvas placement rule. Enlarging UV bias alone did not change the
8,199errors and was discarded. `dialogue_lab.cpp` uses the production capture
with unrelated atlas/placement coordinates, all four origin phases, mixed
ink overlaps and all four scroll steps. `check_dialogue_lab.py` independently
paints and physically scrolls a bitmap before nearest scaling. Old and New
`dialogue-lab-*-r5` both check1,536,000pixels with zero differences; upper
centre and lower output are byte-exact. Each export must match its source
CPU checksum; unhashed r1/r2 exports were inconsistent and are not evidence.

The real BUILD44 phrase «First, talk to the Bob-omb Buddy» and A-driven scroll
were inspected in `sessions/dialogue-p44-old/screenshots/`. Final counters:
1start,156characters,5line commits,20scroll steps,0dialogue failures. 48host
checks plus ASan/UBSan dialogue tests pass. Existing unsupported producers
elsewhere in the game remain in the complete log. The run permits no claim
of stable60FPS or physical hardware acceptance. No new CIA was packaged.

The separate main-menu report remains open. BUILD44 Old-model menu windows
include ~35–37ms mean intervals with ~38–41ms spikes during animated heads;
other transitions have larger spikes. Bridge composition is ~3.56ms in those
windows and GPU submission ~6.9ms, with ~200draws. These overlapping guest
wall measurements cannot be summed into CPU cycles or assigned solely to2D.
Full window records and manual volume-change limits are retained in
`playable-r1/dialogue-build44-validation.json`. Detailed presentation metrics
previously excluded lower-LCD menu geometry. Source now logs both LCDs,
separates scene/display-mode windows, preserves boundary intervals, computes
FPS from actual accumulated intervals, and records source2D preparation.
This instrumentation awaits the next meaningful renderer candidate; it is
not a measured menu optimization.

The next source-resource pass inventories all six cell banks:50cells and188
parts, all4bpp, with no source affine/mosaic/hidden/semitransparent flags.
There are four character atlases, six palette resources and four nonempty
32x32background maps. Private fingerprints and counts are in
`text-r1/menu-resource-roster.json`. This is an inventory, not prepared bank
coverage. Layout1 MAIN uses the first8KiB of characters1 plus the external
icon asset at index0x2c, and replaces the last palette bank from0x2d; SUB
uses the full selected character asset and has language replacements through
`020bcdb8`. Cooking every cell against characters1 alone would give wrong
icons/language variants. Preserve these explicit layout associations before
enabling the native cell/display route in playable mode. Source contracts:
`020b6d40`, `020ade58`, `020bcba8`, `020bcdb8`, `020c33d0`, `020c32e8`.

## Complete OBJ resource preparation (after BUILD44)

`tools/native_renderer/menu_obj.py` now cooks the six cell banks for layouts1
and3, each physical engine and the five distinct language asset sets. Default
languages0/1 share one set; French/German/Spanish/Italian retain their own.
This produces20native image banks with50cells each (1,000images,7,168,000GPU
bytes if every language/layout were resident simultaneously). These are
alternative resource views, not1,000draws or a requirement to load all sets.

The source recipe is explicit. MAIN keeps the title atlas's first8KiB plus
the8KiB icon asset, with palette15 from icon-palette bytes192..223. Localized
loads replace MAIN's first8KiB. SUB copies only the counted localized extent:
French/German/Spanish title resources are80pixels high, so the unmodified
suffix of the128pixel default atlas survives. File-menu translations are
96pixels high. Layout3 English does not reload MAIN's title half. Flattening
these into one generic atlas would lose the correct suffixes and icon colours.

Private output `build/native-renderer/menu-obj-r1/catalog-r2` includes source
fingerprints, individual native banks, twenty view catalogs, previews and a
combined typed OBJ catalog. `menu_obj_catalog.cpp` validates resource/view
contracts once and selects immutable cells by layout, engine and language.
Missing language/layout combinations return no view; they do not substitute
another language. Overlapping routes and mismatched source cell identities
are rejected. This selector is not yet connected to playable asset loading.

The native C++ reader validates all20banks and1,000images, with all24semantic
routes (including the shared default languages). The check runs with
ASan/UBSan and is preserved as `menu-obj-r1/validate-catalog.cpp` and its
executable.50host tests pass, including independent expected source splices,
icon palette colours, route ambiguity and missing-view behavior. Preview
inspection is offline asset evidence; complete menu rendering, source-file
load observation, runtime affine/wrap behavior and GPU parity remain open.
No new game candidate or CIA was created for this resource-preparation pass.
The portable hook library and the complete generated catalog also compile
for ARM11. An existing misleading-indentation warning in `scene_producer`
was corrected without changing its inverse-matrix calculation. This ARM
compile check is separate from linking/booting a playable candidate.

## OBJ loader connection

The original `020bcba8` load boundary now selects native views from layout,
engine and language. `020b3f20` publishes counted file identities/CRCs; no
source pointer survives that callback. The transaction checks localized
NCCG extents and raw icon identities, preserves validated icon resources
across layout changes, and publishes a view only after its base character,
palette and required replacement files agree. Conflicting definitions of a
shared source member are rejected at catalog installation. A bad file revokes
ready views for that transaction; an unsupported layout/language clears both
pending routes. Cell draws select the engine's committed six-bank view.

`--menu-obj-catalog` stages this catalog through the maintained integration
tool. The generated checkout has the combined24-bank image catalog and the
complete OBJ catalog, and links `menu_obj_catalog.cpp`.50host tests pass with
ASan/UBSan, including incomplete loads, changed files, retries/new loads and
layout/language changes. The actual ARM game target links and passes
`PASS_STATIC_LINKED_ABI`. Evidence: `menu-obj-r1/loader-validation.json`.

These seams run when the native2D route is enabled. Playable mode still uses
the bridge and does not yet enable this route: complete menu backgrounds,
effects, drawing-canvas composition and placement policies remain open.
No playable boot, pixel parity or FPS improvement is claimed for this loader
connection, and no numbered candidate/CIA was packaged. The next work is the
remaining menu composition, followed by actual Old/New runtime validation.

## Static backdrop closure and remaining palette effects

`menu_backdrops.py` prepares all four nonempty embedded maps for their
reviewed source associations. Eleven native canvas banks contain52states:
MAIN resource1 has six language inputs and five button-palette map variants;
SUB resources2/3 have five palette states in layout1; resources2/3/4 have
four states in layout3. SUB BG2/BG3 share character base0x4000 (`020bd500`),
so layout3's64KiB character load changes both layers. Layout1 uses the32KiB
atlas; map4, whose tile IDs reach767, belongs to the larger layout3 atlas.

`020ada70` loads208colours into SUB BG, preserving the last48 from its
base palette; `020b850c` replaces the last five font colours. The native
preparer preserves both operations. The source's palette0/backdrop colour
is retained in metadata, separate from transparent tile pixels. The sparse
background cooker now supports8bit indices and a palette per immutable
variant, sharing atlas tiles and equal geometry. No palette expansion is
added to the game loop.

Private assets: `build/native-renderer/menu-backdrops-r1/catalog-r1`.
Native GPU payload for all alternatives is4,585,216bytes; actual residency
must still select only active resources. The native C++ reader validates
all11NMD banks and52parts with ASan/UBSan.50host tests pass. Independent
source comparison uses the original isolated host PPU at vertical offsets0
and64:5,111,808RGB pixels, zero differences. This includes the otherwise
off-screen bottom64rows. The reference uses an authored black backdrop;
this does not verify game backdrop composition, PICA sampling, windows,
layer blends or physical hardware.

Known remaining native effect: `020ada70` also copies source OBJ palette
bank12/13/14/15 into SUB OBJ palette12 to recolour the file's stars. The
current20OBJ views describe their base palettes; these four star states must
be added before enabling complete native menu rendering. Background routing
must also support resources2/3/4 and language-specific resource1 bindings;
the existing single-resource state remains active until replaced. Dynamic
maps5/6/7 (text/canvas), fade/window effects and source placement policies
remain open. No candidate or CIA was created during this preparation pass.

## Native backdrop state and loader connection

`MenuBackdropState` selects the prepared resource/language/layout view and
emits owned canvas placements and scissor rectangles through the existing
native compositor. It tracks all four static map owners and clears command
publication when a source owner retires or its identity changes. Cropping
and the six button-palette writes update semantic state; no background
pixels are rebuilt. Source validation covers external MAIN characters and
palette, and embedded SUB characters/base/colour palettes at load time.
Shared source declarations must agree across native views.

The adapter now observes the static map owner from MAIN resource1's saved
map and resources2/3/4's direct map. `020ada70` updates the selected native
SUB palette state; its original routine still runs while the remaining OBJ
star recolour/fade migration is open. `--menu-backdrop-catalog` stages all
view metadata and the matching native canvas catalog. The generated game
links the11views and a combined12-bank canvas catalog (including the earlier
resource1 comparison bank). Native frame diagnostics use
`SM64_NATIVE_MENU_BACKDROPS_1` when this catalog is installed.

Both engine backdrop colours are explicit native solid layers behind the
ordered geometry/backgrounds. Transparent prepared tiles must not turn the
original palette0 backdrop black. Unit coverage checks colour expansion,
the two LCD routes, wrapped scrolling, tile crop/remainders, prepared palette
selection, invalid map writes, retired owners and atomic refusal before
publication.51host tests pass with ASan/UBSan. The actual ARM game target
links and passes its ABI check. All11NMD resources are byte-identical to the
earlier5,111,808-pixel source comparison. Metadata is validated by the native
C++ reader as well. Evidence: `menu-backdrops-r1/state-validation.json`.

Playable mode still uses the PPU bridge. The next runtime step is an opt-in
native menu3DSX test (noCIA) to inspect remaining effects/placement gaps and
measure the complete route, followed by closing those gaps. Dynamic
text/canvas maps, the SUB star palette copy, fade/window/alpha effects and
widescreen background extension are not accepted yet. No playable pixel
parity, stable60FPS or hardware result is inferred from compilation/tests.

The owner explicitly set the goal to make the entire game native, starting
with complete native 2D and removal of the PPU bridge. No new CIA is permitted
until that migration is complete. BUILD42 was already packaged before this
instruction; it is not native-2D acceptance.

The playable runtime still calls `ppu_prepare_candidates_cached` on the game
thread and `ppu_compose_candidates` on the render worker. Its call to
`set_bridge_2d(true)` makes `native_2d_active()` false: existing menu producer
components do not establish complete playable-native coverage.

First-pass source ownership map:

| System | Semantic boundary and remaining work |
| --- | --- |
| Menu cells | `020c9688` load / `020c0eb0` placement / `020b7384` transfer; prepared bank0 is partial, other banks/layouts/languages need complete coverage. |
| Menu backgrounds | `020adc08`, `020c0638`, `020c0288`; current native bank5001 covers one resource and five variants, not every sky/pattern/background. |
| Glyphs and dialogue | `LoadFont`, `Message::AddChar`, `0201b388` line commit, `0201adfc` scroll, `Message::UpdateWindow`; preserve parser/input/audio/choice state, replace tile writes with retained glyphs, native clipping and panels. Font handle0x980e is the already prepared mask6000; advances, palettes and the alternate font still require their own source contracts. |
| HUD and maps | `dMap_c`, `UpdateMinimap`, `GetMinimapID` and OAM producers; use semantic atlas IDs and affine map transforms, replacing BG3/OAM rasterization. |
| Drawing toy | `c4fcc` capture / `c19cc` strokes; retain the fixed drawing behavior while moving the canvas owner away from bankD/DS pixel layout. |
| Pause/options | Native interface artwork exists, but original map layers and some fades still pass through the bridge. |
| Windows/effects | `MenuDisplayState` records some semantics; layer masks, alpha, brightness, priorities and transitions need complete native consumers. |
| Other game modes | VS, minigames, credits and uncommon producers remain an explicit coverage inventory; the families above are not proof of every overlay. |

The common publication route now accepts frame-owned atlas quad batches in
`ImageLayer`. The same native vertex arena feeds both world geometry and LCD
composition, with disjoint ranges and no per-glyph texture/draw requirement.
Solid native rectangles require no atlas or fabricated texture. Screen clips,
order and the existing compositor remain authoritative. A screen batch between
two world batches prevents erroneous merging across its vertex range. Capture
exports preserve ownership and reject overlaps/unowned spans.

The native A4 path now takes RGB from vertex/tint colour and alpha from the
mask. The previous ordinary RGBA combiner produced black output for A4, even
when the component test reported successful draw calls. The font lab now
requires visible coverage for every nonblack opaque selector. It also leaves
both LCDs in RGB8; `consoleInit` had changed the lower LCD to RGB565 and made
actual lower-screen output corrupt despite correct RGB8 render targets.

Validation: 47 host tests pass; snapshot/image-layer/capture cases also pass
ASan/UBSan. The ARM11 batch lab renders256glyphs on both LCDs, with16selectors,
two batched screen draws and one resident16KiB A4 atlas. Old session
`font-batches-old-r4` checks1,228,800 bottom-LCD pixels against an independent
CPU nearest-sample mask/tint reference: zero channel error; the top centre is
byte-exact to the lower output. Actual UI screenshots confirm readable glyphs.
New session `font-batches-new-r5` passes the same1,228,800-pixel reference and
also shows native solid panels and glyphs on both LCDs. These are component results, not migrated Message producers, game
FPS, full-game coverage or physical-hardware acceptance. Old r1/r2 black output
and r3's lower-LCD format error are not accepted visual results.

## Source contracts

The menu uses its own cell/animation system rather than calling the ordinary
`OAM::Render` entry point for this caption. These pinned original functions
establish the resource and draw identities:

- `020b6d40` constructs character, palette and cell banks from counted resource
  tables. `020c99d8`, `020c9988` and `020c9688` build their runtime objects.
- `020c9a2c` reads the counted NCCG/NCCL/NCOB blocks. The host reader validates
  container versions, extents and complete record coverage independently.
- `020bcba8`, `020bcdb8`, `020c33d0` and `020c32e8` establish character/palette
  loading and the reviewed 2D OBJ tile layout. Native rendering will use image
  identities, without virtual texture addresses or VRAM lookups.
- `020c0b78` selects a live cell/frame, traverses sprite siblings/children and
  advances the original animation once through `020c0d70`. Each cell part goes
  through the typed `020c0eb0` producer.

`menu_oam_hooks.cpp` associates runtime cell spans with logical bank, cell and
part IDs when native mode loads a resource. Sizes and CRCs validate the prepared
cell/character/palette association once at load, without scanning VRAM. Source
ownership is retired by `020c121c`, including the last-cell lookup cache. No
texture bytes or raw graphics packets are retained in frame packets.

With native mode active, `020c0eb0` produces typed placements instead of encoding
software OAM; `020b7384` skips its original OAM transfers. The original animation
selection/traversal/advance still runs once. The maintained comparison mode
continues to call both original functions. Trace logging remains opt-in for
the selected diagnostic frame; normal native frames perform no pixel conversion,
capture IO or diagnostic allocation.

With `SM64DS_FRESH_NATIVE_COMPONENT_CAPTURE=121`, Old and New isolated probes
identify eight caption parts from bank 0, cell 0, engine A, at offset (128, 136),
priority 0, without flips, mosaic or affine transforms. The source resources
and this trace are private evidence under `build/native-renderer/menu-frame-r1`.
At sampled frame 121, each profile has captured 960 parts as 120 complete image
instances, with 959 last-cell cache hits, no unassociated parts and no failures.
This establishes the observed title producer, not other screens or gameplay.

## Live capture and residency

`MenuCellCapture` emits a layer only after every contiguous part of a cell has
arrived with matching offset, screen, flip, priority and consecutive slots.
Incomplete groups, wrapping instances, affine/mosaic states and unsupported
resource layouts refuse the frame. A later failure discards the complete frame.
Whole-cell flips include padded image bounds and reverse the nearest-tie bias.

`SceneFrame` owns the resulting `ImageLayer` values in startup-provided storage.
Logical bank/image IDs, rectangles, materials and presentation order survive
producer input mutation. No source/GPU pointers survive publication. Layer
ordering sorts compact indices without allocating; equal image orders retain
chronology, and each LCD is bounded to 64 layers including the 3D surface.

Typed title initialization routes engine A/3D to the lower LCD, with its native
320-pixel camera aspect. Reviewed mode 1 places its 3D layer at order 1408;
OBJ priorities and lower-slot precedence determine native image order. Other
display modes remain unsupported. Layout 1 loads only the first 8192 character
bytes on MAIN, so cell 4 cannot be admitted there despite its valid full-bank
image. Unprepared SUB language/asset replacements also remain unsupported.

The game requires matching model, merged image and menu-cell catalogs before
native activation. Menu bindings also pin the prepared image identity, so
mixing old colour-policy image metadata with new cell bindings is rejected
even when dimensions and file CRCs are otherwise valid.
Preparation emits `native_menu_cell_catalog.hpp`; pass it
with `--menu-catalog` to the generated-game preparer, or
`SM64DS_NATIVE_MENU_CATALOG_HEADER` to the root CMake build. The image catalog
contains particle banks 0/1 and menu bank 4096. `SceneRenderer` pins all required
banks before eviction, resolves only selected images and shares particle/image
residency. It uses a reusable 256-byte composition array for this two-layer
scene, with no per-frame allocation after the first successful submission.

At the caption checkpoint, runtime capture/renderer workspace was 925,464 bytes, 7,712 more than the
previous caption-preparation checkpoint. This excludes static source registries,
SDK metadata, allocator overhead and other game memory; it is not total RAM.
The subsequent [prepared-background and clipping work](NATIVE-BACKGROUNDS.md)
records the current workspace and composition metadata costs.

## Offline preparation

```sh
python3 tools/native_renderer/menu2d.py --rom /private/owner.nds \
  --output build/native-renderer/private-menu2d --preview
```

The profile pins the supported ROM, the original source contracts and the
reviewed first cell bank. The tool validates all inputs before publishing a new
private output directory. It prepares five images in native image-only bank
4096 and emits normal native image catalog metadata. Other cell banks are not
implicitly accepted.

`nitro2d.py` decodes counted source records on the host. `menu2d.py` bakes
non-affine, non-mosaic, normal-mode cells with equal part priorities. Equal
priority follows original OBJ order: the lower index wins overlaps. Palette
zero remains transparent. Flips are baked into individual parts; source modes
that cannot safely share one native image refuse preparation.

The approved reference has a distinct 2D RGB5 expansion, `(c<<3)|(c>>3)`.
Ordinary replication differs by up to four RGB8 levels. This reference rule is
applied offline and pinned separately; it adds no runtime colour calculation.
The native bank identity also includes the cooking policy, so a prior bank
with different colour preparation fails catalog validation despite a valid CRC.

The five prepared cells total 73,728 GPU payload bytes if all are uploaded.
Only the caption image is resident in its test: 256×16 RGBA8, 16,384 bytes,
versus 131,072 for a full RGBA8 character atlas. The other cells stay on SD.
Eight original cell parts share one native image/draw because their exact
reviewed ordering, placement and masks were prepared offline. The live consumer
validates transformations, source layout and complete part coverage.

## Native GPU evidence

`sm64ds-native-menu2d-lab_3dsx` reads the private `04096.nim` from
`sdmc:/sm64ds/`, uploads only caption 0, and draws it through the paired native
LCD compositor. This is a component test, with black background and no 3D.

```sh
python3 tools/native_renderer/check_menu2d_capture.py \
  --session build/private-caption-session \
  --reference build/private-reference/player-native-bottom.bmp
```

The oracle uses the actual completed original LCD caption at (0, 128),
256×16 pixels, scaled to each physical LCD at 1.25×. The lab uses an explicit
1/64-texel positive sample bias for exact-edge nearest-neighbour ties. Without
it, f24 interpolation selected the preceding texel at some edges: 266 upper
and 271 lower pixels differed per capture. That failed binary/source remains
in the private evidence. Geometry is unchanged; the small bias only resolves
ties under this 2D scaling contract and is not a change to 3D texture sampling.

The corrected Old/New runs each complete 60 frames. All eight captures match
691,200 independently checked pixels with zero channel error, including black
outside the caption. Complete-read guest checksums match the exported buffers.
Warm frames use one draw per caption/LCD, with zero geometry/image uploads or
resource creates. The test owns 16,896 linear bytes plus 518,400 output VRAM
payload bytes; SDK/gfx/allocator overhead is excluded. Guest submit timing is
diagnostic only, not physical Old/New FPS evidence.

The 4,096 unscaled caption pixels also match the original reference exactly.
The prepared bank/catalog/previews reproduce byte for byte. These checks prove
this caption component; they do not prove the complete original menu or game.

## Combined native 3D/2D evidence

The real frame-121 snapshot now includes one bank-4096 image, its live rectangle
and the lower-LCD presentation contract. The component lab replays those values
directly with the captured 3D. Only that diagnostic lab substitutes opaque black
clear and ordinary polygon/fog policy; the full game remains refused.

```sh
python3 tools/native_renderer/check_menu_scene_capture.py \
  --session build/private-combined-session \
  --reference build/private-reference/player-native-bottom.bmp \
  --previous-native build/private-prior-native-3d-session
```

Old/New replay each completes 60 fixed frames. Eight exported images cover
691,200 checked pixels with zero error: original caption fidelity, prior native
3D stability and black inactive upper LCD. Prior native 3D is a stability oracle,
not proof of original 3D fidelity. Captured packets and resulting images match
between profiles. The same game binary with native mode disabled retains both
original reference LCD images byte for byte.

Warm combined frames use 18 draws/104 triangles, 92,416 owned linear bytes,
1,048,576 surface VRAM bytes and 518,400 separately tracked output VRAM bytes.
Compared with the earlier native 3D replay this adds one draw, 16,384 resident
texture bytes and 256 CPU composition bytes. Dynamic 3D quads still upload
1344 bytes/frame; image uploads and resource creates remain zero after warmup.
These are component costs in Azahar, not physical FPS or whole-game speedup.
