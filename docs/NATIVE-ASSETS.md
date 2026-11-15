# Prepared native model resources

Original geometry instructions and texture decoding run in the **host cooker**,
not in `NativeModel::draw`. This first contract covers rigid triangle runs,
literal colours, diffuse/emission and lit-normal provenance, ordinary UV input, native
sampler modes, ordinary normal-generated UVs and RGBA8 images. It does not
establish original actor behaviour.

## Resource preparation and checks

The reviewed menu profiles in `sprite_profiles.json` add eleven paired
texture/palette identities from `texBank_E.bmd` and two from each of the four
other language containers. `sprite_profiles.py` verifies every ROM handle path,
the distinct eleven/two-entry rosters, and the original constructor/rebind
sources and their hashes. `bd1c0` replaces two existing descriptors through
`b22e0` for the selected language. Resource preparation does not establish
visual acceptance of every language flow.

Sprite images have a separate `ImageUse::sprite` association. Model capture
cannot select these additional images accidentally, including the independently
alpha-bearing sprite. Indexed sprite images retain palette-zero RGB and an
alpha mask; native TEV alpha selection provides masked/opaque-zero behavior
without preparing two image variants. The reviewed bank contains twelve images
(one existing model image plus eleven sprite images), rather than 21 with
duplicated zero-colour variants. That saves 508,288 bank bytes in this resource
comparison; it is not a whole-game memory or performance measurement.

The in-memory `ImageBinding` now occupies 20 bytes and includes its use tag.
Native bank image records remain unchanged. Existing model bank payloads remain
identical except for the five explicitly expanded menu containers. The current
catalog verifies 347 banks, 729 model-image choices and 19 sprite-image choices.
Two independent preparations reproduce all 696 files exactly. These associations
prove resource identity, not correct original-menu composition.

Use the existing local Python environment with `ndspy`; do not redistribute a
ROM or cooked game resources with source code. The tools accept the supported
EU revision 0 and restrict private outputs to the ignored `build/` directory.

```sh
.venv/integrated-native/bin/python tools/native_renderer/cook_model.py \
  --rom build/roms/europe.nds \
  --model data/enemy/birikyu/birikyu_elec.bmd \
  --output build/native-renderer/resources/birikyu-elec.nmd
build/native-renderer/host/test_native_model_bank \
  build/native-renderer/resources/birikyu-elec.nmd
.venv/integrated-native/bin/python tools/native_renderer/inventory_cooker.py \
  --rom build/roms/europe.nds \
  --reader build/native-renderer/host/test_native_model_bank
```

The manifest records source/native hashes, source material/geometry references,
colour provenance and prepared image pairs. It explicitly leaves game behaviour
unaccepted. The inventory writes metadata only; temporary banks used by the
C++ cross-check are removed. Source textures not currently referenced by a
material are prepared only through explicit model/animation associations.
The first reviewed profile is Birikyu's electric effect; unprofiled animations
still need their resource/binding contract.

Host tests compare all seven source texture formats against the unchanged
pinned provider decoder, including all 32,768 opaque direct colours, every
encoded A3I5/A5I3 value, all compressed interpolation modes and deterministic
palette/selector variations. Geometry tests cover signed coordinates, relative
wrap, primitive topology, draw order, resource scale, matrix selection, colour
provenance and explicit refusal of unresolved inherited state.

## Binary contract: `SM64NMD2` and `SM64NMD3`

All scalar fields are little-endian. There are no serialized native pointers,
DS display lists, graphics registers or retail runtime structures. This is an
experimental local format, not a published asset compatibility promise.

The 128-byte header contains magic/version/total size; mesh, image, material,
part and pose-matrix counts; table offsets; CRC32 at byte 52; source SHA-256 at
56; and a required-live-material-bindings flag at 88. Bytes 92–127 are reserved
zero. CRC32 covers the entire file with bytes 52–55 treated as zero.

| Table | Record bytes | Contents |
| --- | ---: | --- |
| Mesh | 32 | Vertex offset/bytes, index offset/bytes, layout flag at byte 16; remaining words zero. |
| Image | 32 | Tiled image offset/bytes, width, height; remaining words zero. |
| Material | 64 | Image index or `0xffffffff`, blend, cull, depth/sampler flags, alpha cutoff, six UV floats, RGBA tint; remaining words zero. |
| Part | 32 | Mesh, material, pose matrix, first index, index count; remaining words zero. |

Depth/sampler flags contain depth-test/write at bits 0/1, S wrap at bits 2–3,
and T wrap at bits 4–5. Wrap values are clamp=0, repeat=1, mirror=2; value 3 and
bits above 5 are invalid. Blend is opaque=0/alpha=1; cull is none=0/back=1/front=2.

Tables occur in the listed order immediately after the header. Payload begins
at the next 128-byte boundary. Each mesh contains float3 position, float2 UV and
RGBA8 vertices (24-byte stride), then little-endian uint16 indices, with
four-byte alignment. Version2 requires layout flag0. Version3 permits flag1:
the same first 24 bytes followed by four signed16 lanes, stride 32. XYZ are raw
signed10 normals (-512..511); W is UV source 0 (normal + explicit offset),1
(normal + live material offset), or2 (direct TEXCOORD override). Other flags,
normal ranges and source values fail validation. The writer keeps version2
for resources that do not need the optional payload; all 175 previously prepared
banks retain their exact bytes. Both formats have the same image/UV convention. Images each begin on a 128-byte boundary and contain
Morton-tiled ABGR8 bytes with source rows reversed in storage; UV `(0,0)` is
source top-left. This offline orientation preserves `floor(S),floor(T)` nearest
sampling at exact integer texel boundaries, including repetition and reflection.
Version 1 used the opposite convention and is explicitly rejected, even with
a valid checksum. Retain old banks with their old executable and evidence.
Tint is `0xRRGGBBAA`. Stored default UV transforms are two affine rows of three
floats. Live materials may select fixed or normal UV shader contracts, sharing one
bounded coefficient block. The on-disk material defaults do not encode the
live texture-normal matrix.
Parts use complete triangle ranges and preserve source submission order.

Limits: 256 meshes, 128 images, 512 materials, 1,024 parts, 256 pose matrices,
64 MiB per bank. Actual upload budgets are separately enforced; format limits
are not a promise that every valid bank fits Old 3DS memory.

The reader validates ordered non-overlapping ranges, CRC, enums, references,
finite attributes/uniforms and index ranges before exposing borrowed views.
The caller keeps the input immutable until upload finishes. Failed open or
upload leaves its output unchanged; partial GPU allocations roll back.

## Instance lifetime and live materials

`NativeModel` owns shared meshes/images and exact-sized system-allocated
metadata. Instances supply object-to-clip pose matrices and native material
bindings. Original cooked banks require a complete explicit material array;
omitting it fails before drawing. Defaults alone do not reproduce original UV
animation, billboard transforms, texture selection or visibility.

Diffuse/emission colours remain material uniforms, with white immutable
vertices. Literal source colours stay in geometry. Runs split by bone and
colour source. A triangle mixing dynamic and literal sources is refused until
a suitable vertex/shader contract exists. This preserves the ability to animate
materials instead of silently freezing their initial colours.

Lit runs retain raw signed normals in the existing normal32 layout. Their
catalog association records the bone active at NORMAL and an optional inline
diffuse/ambient override; these are independent of later position-bone changes.
The live specular/emission values remain dynamic. Light flags alone do not
enlarge a mesh that never actually uses lit normals. A triangle requiring
several position/normal bases or incompatible colour roles remains refused.
No graphics instructions are retained in the native bank or runtime bindings.

Upload occurs after the backend begins and drains previous GPU work. A logical
reset can happen after submission; actual GPU resource frees wait until the
next successful begin. The backend object must outlive its assets, and calls
stay on its owning render thread. No per-frame geometry/image uploads are
needed for resident rigid instances.

## First original resource preview

`sm64ds-native-asset-lab.3dsx` loads only a prepared `model.nmd` from its isolated
SD directory. It requires a single-pose planar XY resource and fits it into a
fixed pane; this controlled preview is not its original camera/billboard pose.
It overwrites/frees the CPU bank after upload, renders 600 frames through the
native backend, checks missing-binding rejection and zero resident uploads,
then verifies all owned GPU bytes return to zero.

```sh
python3 tools/native_renderer/run_asset_lab.py \
  --asset build/native-renderer/resources/birikyu-elec.nmd \
  --instance build/native-renderer/private-asset-session \
  --app /path/to/Azahar.app --3ds-model old --timeout 90
python3 tools/native_renderer/check_asset_capture.py \
  --session build/native-renderer/private-asset-session \
  --reference build/native-renderer/resources/birikyu-elec.image-0.rgba
```

Repeat in a new directory with the New profile. The first 32×32 resource uses
integer 5×3 magnification so pixel centres do not lie exactly on nearest-sampler
texel boundaries. An earlier 5×2.5 preview exposed 195 boundary disagreements
per image; those acquisitions remain retained and are not accepted images.
The current check includes internal alpha/texture edges, excludes one pixel
around the outer pane edges and checks the complete exported-buffer checksum.
Mac window screenshots and completed GPU readbacks have distinct provenance;
neither is a photograph of physical console output.

Remaining work includes additional light/table producers, position-generated
UVs, mixed-bone triangles, mixed alpha/depth, native fog, animation/effects,
original 2D engines and complete scene/progression checks. RGBA8 is currently
the exact-colour baseline; compact texture formats and streaming must earn
their memory savings without unmeasured fidelity loss. Emulator results do not
establish physical Old/New performance or 60 FPS.

## Prepared model catalog and live instances

`author_catalog.py` now writes native banks plus an immutable generated C++
catalog. The catalog associates original file identities with source-material
colour roles, image pairs and ordered ordinary/billboard bone references. It
contains no graphics instructions or pixel payload. Source SHA-256 and table
associations are checked against each bank before use. Current preparation
produces 347 native banks, all accepted by the C++ association validator.
Each image identity retains two 16-bit dimensions (four extra metadata bytes),
so live material capture does not retain or revisit the CPU bank after upload.

```sh
.venv/integrated-native/bin/python tools/native_renderer/author_catalog.py \
  --rom build/roms/europe.nds \
  --output build/native-renderer/private-catalog
```

Use a new output directory for each preparation. Pass its `native_catalog.hpp`
to CMake as `SM64DS_NATIVE_CATALOG_HEADER` to compile its immutable associations.
The default build has an empty catalog and retains the resource-only preview.

`capture_instance` consumes semantic live materials and native pose matrices;
it emits owned `PartInstance` values without I/O, allocation, hashing or geometry
decoding. Invisible parts skip image/pose work, and failure leaves the output
unchanged. `NativeModel::draw_instance` validates the visible occurrence before
submitting its parts in authored order. Distinct ordinary and billboard poses
can reference the same original bone without rewriting shared geometry.

With a catalog, the asset preview exercises this instance route, native
ordinary/billboard pose preparation, hidden parts and material colour changes.
It records five captures and separately samples pose/snapshot ticks. Those
inputs are authored live-state tests on an original resource. They still do
not accept its original UV generator, animation, camera or game behaviour.
See [model producer integration](NATIVE-MODEL-PRODUCERS.md) for the live game
entrypoints, measured scope and missing runtime connections.

## Explicit image animation preparation

`animation_profiles.json` records reviewed model/BTP paths, original handle
numbers and the hashes of the original caller/initializer sources. The catalog
preparer checks those source hashes and resolves the original handle paths in
ROM overlay zero before writing any output. Shared names or matching pixels
never establish which model owns an animation.

`texture_sequence.py` resolves BTP texture, palette and material names exactly
as the original `Prepare` helpers do. It validates file spans, strings, selectors
and key ranges. The last eligible key wins, including unsorted or duplicate
starts. Palette-free tracks include the palettes reachable through other
explicitly supplied sequences on the same material. Unreachable keys do not
create extra images; the closure refuses more than 128 identities before pixel
decoding. Original geometry and its UV basis are unchanged.

The default catalog currently profiles model 261 and BTP 262, the 60-frame
Birikyu electric effect. Its two pairs `(texture 0, palette 0)` and `(1,1)` match
the unchanged provider texture decoder exactly. All 60 frame selections refer
to prepared images. The 347-bank C++ check also resolves all 729 prepared image
identities through native instance capture using authored poses/materials.
These checks do not prove actor playback, BTA UV animation or appearance in its
original scene. Only one of the ROM's 105 BTP files has a reviewed association
in this profile list so far.

For a deliberately supplied association outside that list, the one-model CLI
accepts repeated `--animation` arguments. The caller is responsible for the
model/animation association; name resolution alone is not that proof:

```sh
.venv/integrated-native/bin/python tools/native_renderer/cook_model.py \
  --rom build/roms/europe.nds \
  --model data/enemy/birikyu/birikyu_elec.bmd \
  --animation data/enemy/birikyu/birikyu_elec.btp \
  --output build/native-renderer/private-animation/birikyu-elec.nmd
```

With its second 32×32 RGBA8 image resident, the resource preview owns 8,704 GPU
bytes and 156 system metadata bytes, compared with 4,608 and 140 in the earlier
one-image increment. The additional image costs 4,096 GPU bytes and one texture
handle; this is necessary coverage, not a measured memory reduction. Both
profiles pass 600 frames without resident geometry/image uploads, and owned GPU
memory returns to zero at shutdown. Current GPU captures still render the first
image with authored state; selection/playback of the second image in the actual
game remains unaccepted.

## Normal-generated UV source and precision

The host cooker resolves NORMAL and TEXCOORD events into native attributes.
NORMAL computes coordinates with its current raw offset; TEXCOORD can replace
those coordinates immediately. Explicit raw coordinates persist between parts
of a material group; a new group reinstalls the live material offset. A missing
entry normal without a direct-coordinate override remains an explicit refusal.
The rule is corroborated by the unchanged provider and read-only
[melonDS command execution and CalculateLighting](https://github.com/melonDS-emu/melonDS/blob/7d8dea42e221b13521270c423b61add41a741b97/src/GPU3D.cpp).
No command interpreter or code from that reference enters the native runtime.

Each ordinary part records the last valid palette bone used for its texture
matrix. That can differ from the bone moving its vertices. The live producer
normalizes basis columns only when the original 52-byte runtime bone record
requests it, composes the view, then forms the two texture rows with original
half-dimension/sign/word-wrapping rules. Positional scale and translation do not
enter this basis. Billboard mode2 inherits another texture matrix in the
original path and remains refused until that dependency is represented.

The shader evaluates three signed normal products per coordinate, shifts21,
adds the retained offset and wraps signed16 before interpolation. Base32 digit
convolution keeps arithmetic intermediates within PICA float24 precision.
Direct-coordinate source 2 suppresses the dot and material offset. A 16-row GPU
uniform block is shared with the 7-row affine-source integer path; switching
modes invalidates its cached contents. Mesh attributes switch between colour24
and normal32 explicitly. Host arithmetic covers 4,096 signed/extreme cases;
original3x3 composition/normalization checks cover 10,000 cases. These are scoped
contracts, not acceptance of an original lit actor or full scene.
