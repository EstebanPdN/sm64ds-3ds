# Native model producer integration

Current particle checkpoint: the observed title's primary/child billboards now
join the native model/sprite stream. At sampled frames 1/121 the full-game probes
report 0 counted legacy writes and 0 producer failures; all complete scenes remain
refused pending pixel policies and composition. See [native particles](NATIVE-PARTICLES.md)
for current 70-test host evidence, ARM source comparisons and both-screen limits.
Earlier per-increment results below retain their original scope.

The resource bank/backend are now joined by prepared source associations,
native instance snapshots, ordinary/billboard game-pose preparation and real
game ABI hooks. **The maintained game's render loop has not switched to this
route.** The opt-in native owner now links into a generated full-game executable.
It boots and preserves original updates/audio, but rejects the initial menu
frames because producer coverage is incomplete. This is integration evidence;
a complete native scene has not been accepted.

## Implemented boundaries

| Piece | Responsibility | Current evidence |
| --- | --- | --- |
| `author_catalog.py`, `ModelCatalog` | Immutable file/material/image/part identities; binary-search file lookup. | All 347 prepared banks validate their source hash and associations in C++. |
| `texture_sequence.py`, `animation_profiles.json` | Host-only BTP name/key binding and inherited-palette closure for explicit reviewed associations. | Birikyu model 261/BTP 262 corroborated by original source and ROM handle paths; both images match the unchanged decoder, all 60 frame choices prepared. |
| `capture_instance` | Snapshot native pose, visibility, colour role, image choice and material parameters. | Transactional failure, hidden inputs, missing images and later animation mutation checked with sanitizers. |
| `game_model_pose` | Compose live game affine poses and original ordinary/billboard rules, then apply a native projection. | 10,000 compositions compared with the unchanged original game function; 10,000 normalization cases checked independently; ARM builds. |
| `NativeModel::draw_instance` | Submit ordered shared geometry with captured per-part pose/material/visibility. | Original-resource GPU preview passes default, hidden and tinted states in Old/New Azahar. |
| `model_components_hooks` | Observe original file lifetimes and intercept actual `ModelComponents::Render`. | Real game headers compile on ARM; host harness checks forwarding, native refusal, 5,000 lifetimes and address-identity collision. |
| `texture_sequence_hooks` | Capture prepared BTP image identities at the actual animation updater; retire state on component init and last file release. | Host sanitizers check key boundaries, palette retention, refusal, reused addresses, 2,000 lifetimes and bounded table exhaustion. |
| `game_materials`, `game_material` | Copy original 48-byte runtime materials into semantic live values, including selected images, UV math and colour availability. | Actual-header host chain reaches native instance capture; 4,096 UV matrices compared with an independent integer oracle; ARM builds. |
| `prepare_integrated_native_renderer.py` | Stage the new sources, shader and linker seams in a generated game checkout. | Pinned CMake contract and refusal-before-write checks pass. Opt-in runtime installation and full-game ARM linking pass; incomplete frames remain refused. |

These are source and scoped execution results, not proof that an actor runs
correctly inside its original scene. The complete host suite currently has
66 passing CTests, with C++ sanitizer coverage, including catalog-enabled checks.

## Menu sprite producer

`menu_sprite_hooks` observes the original `b2308` descriptor constructor after
allocation and binds its explicit BMD texture/palette index to a prepared sprite
image. `b22e0` updates an existing descriptor's explicit language-image identity.
The original menu frees the BMD after upload, before it finishes drawing those
descriptors. Associations therefore retain immutable catalog identities after
source-file release and retire at the descriptor's own `c1620` destructor,
before its original deallocation. They retain no source BMD/texel pointers.
Native mode replaces `c421c`
with semantic main/shadow submissions; inactive mode forwards the original.
The original CPU rotation helper is retained, while no source graphics writes
or graphics-command interpretation occurs in this producer.

The capture preserves fixed-point object/scale/centre ordering, shadow offsets,
five-bit opacity, per-corner colours, sampler modes, cropped image extents,
tile-map holes/order and signed UV seams. A continuous, uniform untiled grid
collapses to one quad with its original fixed-point endpoint rounding. A wrap
seam prevents that collapse. Geometry is generated into the frame's unused
vertex tail, then committed as an owned ordered occurrence.
When both main and shadow opacity are zero, capture skips the otherwise unused
rotation and affine composition. The observed initial menu invokes three sprite
producers but only two emit geometry; the ARM regression verifies that the
hidden call performs no rotation work.

`run_menu_sprite_arm.py` builds and executes the real 32-bit hook with authored
source-layout objects. It checks main/shadow poses, descriptor lifetime,
alpha-source selection and inactive forwarding. It uses the actual model-file
observer/retirement chain and proves that source-file release preserves sprite
identity, language rebinding changes it, and descriptor destruction removes it
before forwarding deallocation. Its CPU rotation seam checks
the forwarded angles; this test does not establish original rotated visuals.

The 180-update Old/New integration probes now capture two menu quads and the
two-part lit model with no producer failures at the observed frames 1 and 121.
Remaining guarded graphics writes fall from 125/533 to 17/425 units at those
frames; these are counts, not milliseconds. No complete frame is accepted.
Additional descriptor/resource paths, original polygon-ID/fog rules, particles
and background/2D composition remain necessary before menu acceptance.

Profiling is opt-in. Model capture, full sprite-wrapper capture, bank loading,
queue wait, upload and submission have separate counters; a null sprite clock
performs no clock reads. Emulator values remain guest measurements.

## Game pose semantics

The ordinary route composes object/view, optional scale and bone transforms in
the original order, retaining fixed-point truncation and ARM word wrapping.
Source model scale is already cooked into local geometry and is not applied
twice. Only referenced ordinary/billboard bones are computed, once per kind;
two 256-bit masks use 64 bytes of scratch rather than 512 byte flags.

Billboards follow the separate original XY-axis branch, integer normalization,
rounded products, unscaled orientation and scaled translation when a view is
supplied. A regression now detects the earlier reversed scale placement against
the frozen old implementation. Default scale is `(4096,
4096,4096)`, confirmed in original game/provider data. The native normalization
currently admits axis components within ±2²⁴; wider numerical coverage remains
unaccepted. This path computes matrices, not transformed mesh vertices.

The native camera/projection producer must supply `view_to_clip`. It must not
read a legacy graphics matrix stack to disguise an interpreted submission as a
native producer. Whole-scene camera/raster precision still needs comparison.

## Hook ownership and lifetime

The hooks are game-thread-only. Install the immutable catalog and submitter
before file loading. Load observation maps original allocation addresses to
file identities; last-reference release removes that association **before**
deallocation can recycle its address. A conflicting identity poisons the entry
until release, preventing a later frame from drawing the previous model.
The association table is bounded and uses no heap allocation per model call.

The native frame owner explicitly calls `activate`, `begin_frame` and
`end_frame`. A submitter consumes borrowed game state synchronously and publishes
owned native snapshots. Missing models or producer failures invalidate that
frame. In active native mode the wrapper never calls the original graphics
submitter, while the original simulation can continue. Before activation the
comparison route retains its original calls.

The preparer defaults to build/link seams. With `--runtime`, it additionally
installs startup/update/presentation/shutdown ownership in a generated checkout;
its receipt records `runtime_installed=true`, `game_renderer_migrated=false`.
Activation remains opt-in through `SM64DS_FRESH_NATIVE=1`. It refuses the pinned
vendor tree, already-staged destinations and changed source/CMake contracts
before writing. The maintained default game route is preserved.

The menu uses a separate resource table, bypassing `SharedFilePtr` and
`Model::LoadFile`. `menu_model_hooks` observes the original loader's explicit
BMD cases after relocation, resolves overlay0 handles through `func_02018a24`,
and retires identities before the original release callback can recycle storage.
Repeated cache hits do not reregister a load; animation/texture slots are excluded.
The host harness covers another 5,000 direct-menu lifetimes. Runtime now records
four direct model loads. The former missing model, FAT205,
`data/DSMT/face_demo_mariostar.bmd`, is now prepared and captured as two lit
parts with one shared light set. Frame1/121 probes report successful producers,
no missing model and no producer failures; remaining writers/2D still refuse
the whole frame. The source roster includes all 455 top-level BMD IDs, distinguishing an
unsupported model from an unknown allocation. Typed archive observations remain
possible, although archive-native resource preparation is still incomplete.

### Verified installation boundaries

The next integration must modify only the generated game checkout. Source
inspection and the generated opt-in installation use these boundaries:

| Original location | Required native ownership |
| --- | --- |
| `platform/3ds/game_main.cpp`, startup before scene resources load | Install file/model observation before original constructors or actors load models. GPU initialization occurs later and remains separate. |
| `__wrap_func_02019390` in that file | Begin capture before original render work; the current branch starts the old GX frame here. Native mode must not configure or feed that old geometry route. |
| `frame_hook` in that file | Seal/discard a complete native snapshot and present it; preserve audio maintenance, original simulation cadence and graphics-failure frame dropping. |
| `platform/3ds/player_present.cpp`, `player_present` and pipeline worker | The existing path publishes old GX packets and invokes its own renderer. A native owner needs an explicit exclusive branch and completion/lifetime handoff. It cannot pass new resources through this translator. |
| `platform/3ds/viewer_3ds.cpp` | Current C3D/frame/target ownership must be explicitly replaced or handed off. The new backend cannot share a frame with untracked legacy GPU state writes. |

Capture common game pose/material data once; projection/presentation may have
two eyes. Install a counter at remaining graphics writers to reject an incomplete
native frame without decoding their commands. Original actor/input/audio logic
must continue even when graphics are refused. Keep this opt-in until producer,
lighting, effect and 2D coverage makes a complete native frame possible.

## Live materials and image selection

The original runtime material is a 48-byte record distinct from the on-disk
material layout (`func_020462d0`). The adapter reads a fixed 48-byte stride,
including on a host with wider pointers. It retains visibility, alpha/depth,
colour availability, all four RGB5 lighting coefficients, light/table flags and
actual image selection. Missing light/table inputs, unsupported generated
UVs, special polygon/depth modes and unprepared images fail explicitly.

An important source constraint: `func_02046bbc`, used by
`TextureSequence::Update`, changes the texture flags/palette address but does
**not** always update the runtime record's initial `textureId`/`paletteId`.
Reading those two initial IDs in `ModelComponents::Render` would freeze some
animations. The new wrapper captures semantic identities at that updater,
including prior animated palette retention when a track supplies no palette.
It does not infer identity from equal pixels or virtual VRAM addresses. Valid
updates still call the original material-data updater; removing that duplicate
key-selection work requires a verified replacement of its non-graphics writes.

The 512-entry game-thread table checks both component storage and model-file
identity. Invalid native updates poison the component; a valid update or real
component initialization can recover it. Last-reference file retirement removes
its entries before storage reuse. Table exhaustion fails closed until startup
reset. Source BTP pointers remain trusted live loader storage: count checks do
not prove backing allocation lengths or constitute a validated BTP loader.

`capture_materials` writes borrowed frame scratch, indexed by original material
ID, and visits each referenced material once even when it has multiple colour
runs. Ignore scratch after failure; only successful `capture_instance` output
may be published. A 64-byte visited mask avoids per-call allocation. Image
dimensions reside in compact catalog metadata, so the CPU bank can be freed
after upload. Additional BTP-referenced image/palette pairs still need explicit
offline preparation. The current catalog adds the reviewed Birikyu effect
profile; unprofiled BTP files cannot be assumed to have complete image coverage.

For UV mode 1, `func_02044b30` derives its matrix from current scale, sine/cosine,
translation and texture dimensions. The original generated coordinates also
truncate and wrap to signed 16-bit fixed values. The native preview's explicit
identity UV is not that producer. The adapter now implements the recovered
integer formula, original texture dimensions and low twelve angle bits from
the original 4,096-pair sine/cosine table. The selected image can differ from
that original texture; vertex UV basis, matrix dimensions and sampled image
dimensions remain distinct.

The fixed UV vertex shader splits products into bounded components to preserve
integer results through float24 arithmetic, then shifts and wraps signed 16-bit
coordinates before interpolation. Host execution of the actual shader branch
checks 2,048 cases with float24 truncation; the PICA laboratory independently
checks 240 cases under clamp/repeat/mirror on both Azahar profiles. These constant
UV fixtures do not accept whole-scene interpolation, camera or original actor
appearance. Normal/position-generated UVs and mixed-alpha/depth remain open.

The original `SM64NMD1` top-left-at-`(0,1)` convention chose the adjacent row at
some exact T boundaries. `SM64NMD2` reverses source rows offline and uses native
top-left `(0,0)`, preserving nearest floor addressing without a runtime epsilon.
Earlier failing acquisitions are retained and excluded from acceptance.

## Timing and memory scope

Game hooks accept an optional coarse callback clock. It is null by default,
so ordinary calls read no clock. `producer_ticks` covers only the callback;
GPU command submission, queue completion and display pacing need their own
scopes. The preview separately records pose/snapshot ticks, backend submit
ticks, wait ticks and the SDK's prior queue duration. Do not add overlapping or
different-frame scopes as if they were one frame distribution.

The catalog-enabled preview passed 600 frames in each Old/New Azahar profile:
five images/profile, 460,980 checked pixels/profile, no evaluated mismatches.
It retained 4,608 owned GPU bytes with zero new geometry/texture uploads in
resident frames and zero owned GPU bytes at shutdown. The earlier v1 checkpoint
reported 108 model metadata bytes; that is historical and predates the larger
fixed-UV material. The later two-image Birikyu bank owns 8,704 GPU bytes and 156
metadata bytes. Read each increment's exact memory receipt and input images.
Those counts exclude catalog/static data, instance scratch, targets, C3D command
buffers, SDK/readback storage and whole-game RAM. Guest tick samples do not
establish physical console milliseconds, FPS or Old/New speedup.

In the 241-draw fixed UV fixture, independently cached state reduces sampler
binding to one or two per pass, with one tint upload and four pipeline changes;
the 241 UV uploads remain necessary for distinct matrices and float restoration.
This is an operation-count result, not a measured physical speedup.

Next: expand native lighting/material coverage, and migrate the menu's remaining
ordered producers and 2D presentation. Continue animation-image profiles when
real scenes require them. The synchronous scene owner, immutable frame values
and unknown-writer refusal are installed, but full progression is unaccepted. Native GPU
uploads/draws must stay on the owning render thread, not inside the original
simulation callback. Physical Old/New stability and sustained performance remain
acceptance work.

Ordinary normal-UV instances also capture a separate texture-normal basis from
original runtime bones. Their prepared part retains coefficients/offsets, not
borrowed source matrices. The last palette basis is distinct from positional
bone selection. Actual-header tests continue through material capture,
normalization, view composition and an owned snapshot; later game mutations do
not alter it. Billboard/inherited-normal entry dependencies still fail rather
than assuming a basis. Lit normals retain a separate bone association fixed at
their original NORMAL event; later position changes cannot silently relight
them with another bone. Frame-owned light/basis/material palettes reach
`NativeModel::draw_prepared` without retaining game pointers. The initial scene
still needs its remaining ordered writers and 2D producer migration.
