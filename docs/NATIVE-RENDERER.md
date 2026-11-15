# Native renderer development

The maintained port's live Options/pause publications now have a renderer-owned
[native interface consumer](NATIVE-INTERFACE.md). This is component integration;
the complete original game scene is still refused until its remaining display,
font, background and pixel-policy producers are migrated.

The new renderer under `source/native_renderer/` has its own mesh, texture,
material and frame contracts. It submits indexed Citro3D geometry through a
new PICA vertex shader. Its API does not include DS commands, virtual graphics
registers, `ntr` types or the existing native mesh implementation.

**The complete game's renderer has not been replaced.** A generated full-game
executable now links the new scene, resource and GPU owner behind
`SM64DS_FRESH_NATIVE=1`. The maintained default remains the comparison route.
Native mode captures high-level model/material inputs and refuses incomplete
frames rather than submitting a partial scene or using the old graphics path.
Additional light/table producers, position-generated UVs, fog, effects, complete animation and both original 2D
engines still need coverage and original-scene acceptance.

Typed [display-state capture](NATIVE-DISPLAY-STATE.md) now retains the title's
brightness/blend masks and live background publications, preserving original
animation state while skipping those helpers' old graphics transfers. These
states still need native background/font/effect consumers; their capture does
not enable original scene admission.

The initial 180-frame Old/New Azahar integration probes boot and shut down
without unmapped accesses, but submit **zero complete native frames**. The
menu's private loader identifies FAT205,
`data/DSMT/face_demo_mariostar.bmd`, now prepared and captured as two native lit
parts. The observed captures have one shared light set, two lighting states
and no model-producer failures. Menu sprite capture adds two quads without
producer failures in the observed initial-menu probes. Typed primary/child
particle capture adds the observed title billboards, and counted legacy writes
are 0 at sampled frames 1/121. Original polygon-ID/fog pixel policies, transparent
clear and incomplete 2D composition still invalidate frames. The live title
caption is now one frame-owned image layer, routed with 3D to the lower LCD.
Its combined replay matches original caption pixels and prior native 3D output;
it still substitutes unsupported pixel policies explicitly. These probes establish integration/refusal behavior,
not native game visuals or performance. See
[producer integration](NATIVE-MODEL-PRODUCERS.md).

The particle increment adds image-only banks and selective residency with
transactional extension. Its 70 host checks, 2,048 original 32-bit producer
comparisons, six GPU regression runs and remaining screen/composition boundaries
are documented in [native particles](NATIVE-PARTICLES.md). The reference title
routes 3D to the lower screen, which the new compositor must preserve.

A native GPU surface/LCD compositor now has authored-scene tests for both
screens, intermediate alpha, batched2D geometry, output tone and paired-frame
refusal/recovery. Its RGB8 output targets save172,800 colour-payload bytes over
RGBA8 at the same dimensions. It still needs connection to original screen and
layer producers; this is not original-menu acceptance. See
[native compositor](NATIVE-COMPOSITOR.md).

## Current implementation

- Native quads share the model scene's ordered occurrence stream. Only
  adjacent identical state merges; a model, image, pose or material change is
  an ordering barrier. Frame-owned vertices, states and logical image IDs
  survive mutation of producer scratch. Producers can write directly into the
  unused capture tail, avoiding a second scratch allocation/copy.
- A persistent quad stream allocates its index pattern once. Fenced updates
  flush only the active vertex prefix; capacity, invalid input, retirement and
  phase errors are explicit. No per-frame mesh allocation or index upload.
- Sprite-only bank residency uploads only the images referenced by the frame,
  with no model geometry allocation. All requests for a file are collected
  before loading; later sprite images retain the earlier working set. A model
  request explicitly promotes that resident bank to its geometry contract.
  The two-image authored lab first uploads one 256-byte sprite image, then
  verifies promotion and subsequent frames without texture/static-model uploads
  or allocation growth; only active quad vertices are uploaded. Whole-bank
  read/validation remains load-time work. Its reported bank-load cost includes
  validation and resource creation; it is not isolated storage latency.
- A short affine/unlit shader entry shares code storage and uniform locations
  with the general kernel. Lit/fixed/normal-UV draws retain their kernel;
  lighting rows survive intervening sprite draws. Switching entries does not
  resend shader code. A pass retains the last entry instead of forcing a
  redundant general-to-affine switch. Counters distinguish draws and binds.
  General-kernel UV controls have their own cache: a short unlit affine draw
  cannot hide a stale fixed/normal-UV mode from the next lit affine draw. The
  GPU regression reproduces 4,200 incorrect pixels before that correction and
  zero afterward, including identical materials across the entry transition.
- Indexed sprite images retain palette-zero RGB and an alpha mask. The native
  TEV alpha source selects masked or opaque-zero behavior using the same image;
  independent source-alpha formats retain their alpha. Neither a sampler change
  nor an alpha-source change decodes or copies texels.

Original sprite polygon IDs and fog are captured but currently refused by
`SceneRenderer` until their native pixel policies are implemented. They cannot
be ignored to make a partial menu look accepted. A transparent 3D clear also
requires the pending native background compositor before RGB8 LCD transfer.

`sm64ds-native-quad-lab` tests ordered models/quads, gradients, changing alpha,
one-image colour-zero masking, adjacent batching, stream lifetime and owner
reinitialization. It renders identical authored scenes through both shader
entries and exports completed GPU targets. `check_quad_capture.py` checks
analytic colours/order plus an exact full-image comparison. These ROM-free
fixtures do not establish original-menu visuals or physical FPS.

- Persistent indexed meshes, immutable tiled RGBA8 textures and typed resource
  handles with generation and owner checks. No static geometry copies during
  resident frames.
- Explicit linear resource budget, accounting and high-water mark. These
  counters cover owned mesh/texture allocations, including alignment and the
  neutral sampler; they exclude the caller's render targets, C3D command
  buffer, metadata, SDK allocations and laboratory readback storage.
- Ordered opaque/alpha draws, alpha cutoff, culling mode and reversed depth.
  Native clamp, repeat and mirrored-repeat samplers, UV affine transforms and
  RGBA material tint are explicit material state. Changing a sampler never
  copies texture pixels.
  Adjacent equal matrices/materials avoid repeated uploads. No transparent
  sorting or changes to original content are implied.
- Fixed UV materials evaluate signed integer matrix arithmetic and signed
  16-bit coordinate wrapping in the vertex shader. A load-time mesh summary
  proves its UV lattice/range without scanning vertices during draws.
  UV, tint, sampler and pipeline state are cached independently, so a changed
  UV matrix need not rebind an unchanged texture or resend depth/blend state.
- Upload phase followed by ordered drawing passes. Resource mutation is
  allowed only after a successful C3D frame begin drains the previous GPU
  queue. Frame end alone is not a resource completion fence.
- Explicit publication of written resources and command fragments. A later
  target clear publishes prior drawing before enqueueing its memory fill.
- Optional tick measurements, disabled by default, plus draw, bind, upload and
  resource counters. Queue time is the SDK's previous completed queue scope.
  These scopes cannot be added indiscriminately or treated as physical timings
  when acquired in Azahar.
- `ModelBank` validates the versioned `SM64NMD2` format without allocating or
  decoding original graphics instructions. `NativeModel` uploads immutable
  resources transactionally, copies only its required metadata and shares GPU
  resources between instances/eyes. The original CPU bank can then be freed.
  Logical model release defers GPU frees until the next completion fence;
  release during the upload phase can reclaim immediately after that fence.
  See [native assets](NATIVE-ASSETS.md) for the format and cooker boundaries.

`Citro3DBackend` has one render-thread owner. The caller owns C3D initialization,
render targets, display routing and shutdown. Other renderers cannot alter GPU
state inside its frame. Call `shutdown()` before `C3D_Fini()`. Compiled shader
storage remains alive for the backend's lifetime. Handles are local to that
backend object and must not outlive it.

## Native vertex lighting

The PICA shader now computes signed-integer normal transformation and up to
four lights, diffuse/ambient/specular/emission channels, the original specular
reciprocal and optional 128-entry shininess table. Base-32 product splitting
keeps the required integer bits exact in PICA float24. These are native typed
uniforms and immutable normal attributes; the draw path interprets no DS
graphics commands and performs no CPU per-vertex lighting.

`Citro3DBackend::draw` accepts an optional synchronous `LightingInput`. It
validates all inputs before mutating GPU state and requires a normal32 mesh.
The caller may release its input after the call. `LightingCache` owns its
source values and detects mutations without relying on pointer identity or
generation counters. Equal lighting avoids both packing and uniform uploads;
normal, direction, colour, material and table groups become dirty separately.
An unlit draw preserves those groups, while a new pass invalidates their GPU
residency. Unlit fixed/normal-UV draws explicitly disable the kernel's lighting
branch; ordinary affine/unlit draws use the separate short entry.

Per-frame statistics expose `lighting_preparations`, `lighting_uniform_rows`
and `lighting_state_changes`. They count CPU packing, float rows sent to C3D
and boolean/loop state changes respectively; they are not CPU/GPU time or
physical bandwidth measurements. The linked code contains a 368-instruction
general kernel and a 14-instruction affine entry. The general DVLE uses 93 float
rows including constants; the affine entry reads 7 dynamic rows and one constant.
These static counts do not describe the dynamic cost of every draw or prove a
physical speedup. Both entries share a DVLP, avoiding repeated code uploads.

The 128-case GPU laboratory runs 120 frames per Old/New Azahar profile and
compares two complete-readback captures per profile with independent wide-
integer colour equations. All 155,392 evaluated pixels agree exactly. The
laboratory also checks repeated-draw caching, light-mask compaction, table
changes, normal32/colour24 switching and missing-input refusal. The actual
assembly has a separate 1,024-case float24 host oracle. The current host suite
contains 66 passing checks with C++ ASan/UBSan enabled. The separate 32-bit ARM
menu-hook regression uses authored objects and the detected SDK, not a ROM or
physical GPU.

The game producer now captures the four live RGB5 material coefficients and
light mask/table flag. Offline runs retain inline diffuse/ambient overrides
and the palette bone active at NORMAL, independently of later position-bone
selection. Texture-normal and light-normal bases are distinct; an ordinary
basis already computed for the same capture/bone is reused. Billboards follow
the original unscaled orientation and scaled translation order, then normalize
their final facing basis for lighting.

`SceneFrame` owns shared light sets and normal/material records in bounded,
startup-allocated palettes. A part retains a two-byte lighting index. Hash
interning avoids quadratic per-frame searches; failed occurrence capture rolls
back new palette entries and leaves output unchanged. `NativeModel` resolves
the published indices during synchronous submission. The 128-case prepared
model laboratory checks this full bank/snapshot/model/backend chain in both
Azahar profiles against the same independent integer colours: another four
captures and 155,392 evaluated pixels, all exact. Borrowed capture inputs may
change or die before those GPU draws.

The catalog now contains 347/455 resources, including 172 newly admitted lit
models. All 175 earlier bank payloads retain their exact bytes. This remains
resource coverage; the original menu still fails whole-frame admission because
of other writers/2D, and no complete original scene has visual acceptance.
The full-game capture/renderer workspace is 656,832 bytes, 160,496 more than
the kernel/backend checkpoint. Shared lighting storage is bounded at 128 light
sets and 2,048 normal/material states; SDK and other game allocations are extra.

## Experimental scene owner

`SceneFrame` copies poses/materials and logical prepared image indices into
caller-owned startup storage. It retains no borrowed game pointers or GPU
handles. A later producer failure, unknown writer or capacity error discards
the entire frame. The current owner is synchronous and mono; cross-thread
publication and game stereo are not implemented by this owner. Native image
layers now share frame ownership and image residency with the 3D component.

`SceneRenderer` owns C3D, its render surface and paired LCD compositor. Its 64-entry resource cache pins
all current-frame assets before admitting new ones, evicts only unpinned
resources after GPU completion, and frees the CPU bank after native upload.
The bank-read allocation is bounded to 8 MiB; resource residency has a separate
8 MiB budget in the experimental game. The caller's capture/producer workspace
currently occupies 925,464 system bytes. These are separate budgets, not total
application RAM; targets, command buffers, SDK and game allocations are extra.
Admission can refuse a scene that exceeds them.

After the previous queue completes, LCD output is detached before drawing and
reattached only for a successful whole frame. The backend also reserves command
buffer headroom before each draw; large frames are refused, not split into
multiple queues yet. Queue fences and output ownership follow the official
[Citro3D render queue](https://github.com/devkitPro/citro3d/blob/v1.7.1/source/renderqueue.c).
The semantic projection maps the original CPU matrix into PICA clip/target
coordinates, following the SDK's
[tilted perspective convention](https://github.com/devkitPro/citro3d/blob/v1.7.1/source/maths/mtx_persptilt.c).

`SM64DS_FRESH_NATIVE_PROFILE=1` enables producer ticks and backend wait,
upload/submit and previous-completed-queue measurements. Bank-load ticks include
upload and must not be added to upload ticks. Producer/bank totals are cumulative;
backend values describe the last attempted native draw. Convert system ticks
using 268,111,856 ticks/second. Zero values with zero accepted frames are missing
rendering coverage, not a speed measurement. Azahar values remain guest timing.
Default runs avoid these per-operation clocks. Old diagnostics that require
the previous renderer's targets are blocked in this route; native screen/resource
dump integration remains pending.

## Validation laboratory

Host checks, with no ROM or SDK:

```sh
cmake -S . -B build/native-renderer/host \
  -DSM64DS_NATIVE_RENDERER_LAB_ONLY=ON \
  -DSM64DS_ENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native-renderer/host --parallel 4
ctest --test-dir build/native-renderer/host --output-on-failure
```

Build the independent ARM executable using the installed SDK:

```sh
cmake -S . -B build/native-renderer/arm \
  -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/3DS.cmake" \
  -DSM64DS_NATIVE_RENDERER_LAB_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/native-renderer/arm --parallel 4
```

It exercises texture orientation, indexed subranges, near/far depth, alpha,
two eye targets, adjacent state reuse, failed resource transactions, stale
handles, same-target clear ordering and shutdown. Eye offsets here test target
routing, not a perspective stereo camera or physical comfort. The extended
fixture also exercises failed partial model upload rollback, destruction after
submission, required live material bindings, UV/tint changes and independent
sampler changes on a shared texture.

For an isolated functional acquisition, pass the actual local Azahar bundle:

```sh
python3 third_party/sm64ds-3ds/build3ds/run_isolated.py \
  build/native-renderer/arm/sm64ds-native-renderer-lab.3dsx \
  --app /path/to/Azahar.app --instance build/native-renderer/private-session \
  --3ds-model old --timeout 90
python3 tools/check_native_renderer_capture.py \
  --session build/native-renderer/private-session
```

The runner requires the existing coordination lock described in its source;
it copies the bundle and creates a fresh profile. Repeat with a different
instance directory and `--3ds-model new`. Its functional `PASS` must be followed
by the independent image check. A process exit or successful draw counter is
not image acceptance.

The image oracle compares analytic rectangle/material expectations against the
actual captured pixels. It excludes two pixels around fixture edges and allows
one channel unit of error. The capture checksum verifies the complete CPU
snapshot before file export. Earlier attempts exposed a wrong-handed test
projection, a texture row inversion and incomplete/unreliable capture exports;
those failed acquisitions remain under the ignored local evidence directory.
The exact cause of the earlier checksum-free export corruption is still open;
it is not declared a solved hardware issue.

On 2026-10-26, the foundation laboratory passed 1,200 frames in each Old
and New Azahar profile with 331,575 checked pixels/profile and 1,792 resident
bytes. The extended model/sampler laboratory adds UV/tint, repeat and mirror
captures: seven images, 573,450 checked pixels/profile and 2,304 resident
bytes. Check the ignored local acquisition receipts for the exact binary and
Old/New acceptance of an increment; earlier receipts do not certify later
source changes. The separate first-resource preview checks three images and
276,588 pixels/profile over 600 frames. The catalog-enabled variant adds hidden
and tinted captures, checking 460,980 pixels/profile. The fixed UV laboratory
checks another 78,240 pixels/profile across 240 arithmetic cases and three
sampler modes. These workloads do not establish full-game speed, total
application memory or physical
Old/New 3DS acceptance.

## Source inventory for the cooker

The host-only `tools/native_renderer/audit_models.py` inventories original BMD
tables, part palettes, geometry commands and texture formats with explicit
range checks. It does not become part of the runtime renderer.

```sh
build/python/bin/python tools/native_renderer/audit_models.py \
  --rom /private/path/europe.nds
```

The supported EU revision has 455 top-level BMD files and 158,738 vertex
commands. The initial scan reads all 455 without a structural failure. The
command set includes matrix binding, colour, normal, UV and vertex inputs,
primitive boundaries, and 97 diffuse/ambient updates. Those in-list material
updates must be compiled too; merely removing the vertex/FIFO loop would leave
state dependencies behind. Counts are inventory, not converted model coverage.

The cooker admits 347/455 immutable model resources; the C++ bank reader and
association validator independently accept every admitted payload. First
blockers for the remaining resources are mixed-bone triangles (59), mixed
texture alpha/depth (43), mixed colour-source triangles (5), and one compressed
texture palette range. This is first-blocker classification, not an exhaustive
list of each model's missing features. All 455 still need live game/scene
acceptance; converted resources are not migrated actors.

Next work: migrate the remaining ordered writers and 2D menu producers, then
add native bone-palette triangles, mixed alpha/depth and remaining light/table
producers. Preserve live visibility, materials, UVs, textures and billboards.
Validate complete attribute identity, mutation generations, matrix precision,
draw order and resource retirement before removing the comparison path.

## Reference decisions

Local DuskLight and Mario Kart investigations were read alongside their actual
implementations. The reviewed public examples supply architectural evidence:

| Reference | Applied decision | Boundary to preserve |
| --- | --- | --- |
| [OpenLara Citro3D backend](https://github.com/XProger/OpenLara/blob/3b268ca4bf986c380f44a26ffd9218bf6d376249/src/gapi/c3d.h) | Separate static residency, dynamic data and native target ownership. | Another engine's frame-ring assumptions are not this game's completion contract. |
| [MKW native renderer](https://github.com/MiniBandit/MKW-3DS/blob/f89d817ee44b1781bbe4e08dd853b92b38233376/src/renderer.cpp) | Prepare target resources off-device; preserve ordered state reuse and explicit matrix/pose ownership. | Its recreated game and quality profiles do not prove SM64DS speed or fidelity. |
| [Animal Crossing native platform](https://github.com/paaime/ac-3ds/tree/e7d1f7244f8d964d5234dc1d69ed2a48e507db20/platform/3ds) | Account for real launch memory and retain resource identities through consumers. | That project still has graphics translation and game-specific memory constraints. |
| [Forsaken shader](https://github.com/colbyshores/forsaken-3ds/blob/dcdc6fe435bb7afa1d5f5ca9c0f00c467cacfb06/shaders/render_c3d.v.pica) | Move supported vertex work to PICA and measure shader-path coverage. | Position-derived normals and its lighting/fog rules cannot establish DS equivalence. |
| [Citro3D queue](https://github.com/devkitPro/citro3d/blob/v1.7.1/source/renderqueue.c), [texture API](https://github.com/devkitPro/citro3d/blob/v1.7.1/source/texture.c) | Drain queue ownership, publish each command fragment and supply valid unused samplers. | `FrameSync` waits for display cadence; a `Sync` transfer called inside a frame queues work. Unit 1/2 null samplers are unsafe in the reviewed API. |

No external renderer implementation was copied into the new module. The
unchanged pinned provider texture decoder is linked only into a host test
oracle; it is absent from the ARM native renderer/laboratories. SDKs were
not upgraded. Geometry quantization, compact native texture formats, conservative
visibility, GPU lighting/fog, batching, capture residency and thread overlap
will be admitted according to measured cost and semantic coverage. Using every
GPU feature is not itself a performance target.

The final target remains complete production rendering without runtime DS
graphics interpretation, preserving the original game's simulation and
appearance. Sustained 60 Hz presentation is a physical measurement to earn;
the original 30 Hz simulation must not be accelerated to manufacture it.

The [native component capture](NATIVE-COMPONENT-CAPTURE.md) now records real
owned model, quad and lighting snapshots without source commands or pointers.
Its replay lab drives the [dual LCD compositor](NATIVE-COMPOSITOR.md), preserving
the prior direct native image and verifying both physical coordinate systems.
This is component evidence only: original 2D/pixel-policy integration, complete
scene acceptance and physical Old/New timings remain unfinished.

The [original menu 2D migration](NATIVE-MENU-2D.md) now captures the live
caption cell through the menu's own typed producer and prepares its pixels
offline. Its standalone GPU outputs match the reference caption exactly on
both LCDs. The live 2D consumer also composes the actual frame-121 caption and
3D component on the captured lower LCD. Complete original-frame acceptance
remains pending; this component lab does not open that gate.

[Native prepared backgrounds](NATIVE-BACKGROUNDS.md) add sparse 2D mesh/atlas
residency in a separate canvas namespace. The first prepared resource matches
the original PPU in isolated dual-LCD tests. Live map cropping, palette edits
and font production remain required before that background can be admitted
as original scene coverage.

The ordinary normal-UV path now has an optional 32-byte vertex payload, exact
signed arithmetic, source-event handling and live original basis/material
capture. Its 240-case GPU fixture checks all three sampler modes, direct UV
overrides and restoration of both integer-affine and ordinary float paths.
The resource preview retains 8,704 GPU bytes /156 metadata bytes. At the closed
normal-UV checkpoint, game capture/workspace occupied 495,048 bytes (9,224 more
than the prior scene checkpoint).
These extra system bytes chiefly hold 256 native normal bases. Matrices are
computed only for visible normal-UV materials and referenced bones. No
per-vertex CPU transformation or resident geometry/image upload was introduced.
