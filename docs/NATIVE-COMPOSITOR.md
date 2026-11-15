# Native GPU surfaces and LCD composition

The new compositor is implemented and tested with authored scenes. **It is not
yet connected to the original game's complete screen/layer producers.** The
fresh game's original-frame refusal gate remains in place. No physical FPS or
complete original-menu acceptance is claimed.

## Ownership and GPU flow

`Citro3DBackend::create_surface` creates a native RGBA8 render texture in VRAM,
with optional depth24/stencil8 storage. Both allocations share the backend's
resource budget. Colour and depth payloads count as VRAM; ordinary images,
meshes and the neutral sampler count as linear memory. `Statistics` reports
those categories separately; `owned_gpu_bytes()` sums them. SDK metadata,
allocator overhead and externally owned LCD targets are outside these counters.

The detected Citro3D SDK prohibits deleting a render target during an open
frame. Surface creation and explicit deletion therefore occur between frames.
Queued retirement invalidates the handle immediately, then deletes the target
before the next `C3D_FrameBegin`; ordinary resources retire after that fence.
Shutdown follows the same ordering. Generation counters prevent slot reuse from
reviving an old surface handle.

A new surface must first be cleared. Sampling it before initialization, sampling
the active render destination, drawing depth-tested geometry into a colour-only
pass, or selecting an out-of-bounds viewport is refused. Viewports can occupy
part of a power-of-two render texture. GPU framebuffer/texture cache transitions
use the SDK's public pass/bind operations, without CPU pixel transfers. Surface
UV orientation follows framebuffer axes; composition supplies its explicit
transform. The unrotated Y-up fixture needs a V flip when displayed in LCD
coordinates. Imported bank images retain their offline orientation.

Transparent intermediates use premultiplied RGB. `Blend::premultiplied` composes
them with RGB factors ONE and ONE_MINUS_SRC_ALPHA, avoiding a second alpha
multiplication. This native runtime mode does not change the accepted blend
values in existing model-bank versions 2/3.

## Two LCD outputs

`ScreenCompositor` owns native RGB8 targets for the 400×240 upper and 320×240
lower LCDs. Their colour payload totals 518,400 bytes, compared with 691,200 for
RGBA8 targets of the same sizes: 172,800 fewer VRAM payload bytes. This is a
component allocation comparison, not a claim that the whole engine uses less
VRAM or runs faster. Output storage is tracked in compositor metrics separately
from backend-owned input surfaces.

A `ScreenCanvas` contains an opaque clear colour, native layers and an optional
whole-output tone. A layer has a native material, physical-pixel rectangle and
optional prepared mesh/index span. Empty mesh selects the resident unit quad;
a prepared mesh supports a batched sprite/tile stream in one draw. The native
portrait clip transform is checked against the installed SDK independently for
both LCD widths. Layer geometry is transformed on PICA, not on the CPU.

Both canvases and resources are validated before either output is drawn. Outputs
remain detached until the complete pair succeeds. A bad lower screen cannot
publish an otherwise valid upper prefix, and diagnostic capture rejects the
refused pair. Subsequent complete frames recover normally. Whole-output tone
at full black/white uses a clear without sampling or drawing hidden layers.
This operation is not a replacement for original per-layer target masks,
windows, blend rules or source brightness semantics; those need typed producers.

Normal frame submission does not read pixels back, upload intermediate images
or recreate geometry. The separate diagnostic API reads completed RGB8 targets
only after frame end, into caller-owned aligned memory. These are GPU target
readbacks, not physical-console LCD screenshots.

## Regression scope

`surface_lab.cpp` covers selective surface lifetime, VRAM accounting, allocation
failure, cropped viewports, transparent clear, partial alpha, same-frame sampling,
source changes across frames, stale handles and shutdown. The initial test
consumer had an incorrect V orientation: its application completed, but image
checks found 9,440 wrong pixels per capture. Those files/binaries remain local.
The corrected consumer passes the independent image oracle.

`compositor_lab.cpp` covers both RGB8 LCD sizes, alpha/mask changes, a batched 2D
mesh, a GPU-generated partial-alpha intermediate, whole-output tone, clear-only
outputs, paired refusal/recovery and failed-initialization rollback. The VRAM
allocator must be initialized before comparing free-space counters: libctru
reports 0 before its first allocation. The leak test establishes that baseline
and verifies that failed initialization returns all target storage.

The laboratory uses the same backend as the game candidate. Its images are
analytically compared, including intermediate alpha, with a one-channel-unit
tolerance. Guest SDK timing is logged solely as diagnostic information; it is
not physical Old/New 3DS timing. Original screen routing, both original 2D
engines, backdrop/fades, source polygon-ID/fog/depth and whole-game acceptance
remain necessary. The reference title at frame 121 sends 3D to the lower LCD.

Prepared images, captures, binaries and failure preimages stay under ignored
`build/native-renderer/`. No release or publication is part of this work.

## Scene-owner integration

`SceneRenderer` now renders its native model/quad scene into a 256×512 RGBA8
render texture with depth/stencil. It uses a 240×400 or 240×320 viewport, then
samples that viewport through `ScreenCompositor`. The tilted camera swaps and
reverses the sample axes; unused power-of-two padding is excluded. The caller
supplies matrices for the selected physical LCD's aspect. No intermediate
pixels or transformed vertices return to the CPU during normal frames.

The [real component lab](NATIVE-COMPONENT-CAPTURE.md) verifies that the top
output matches the prior direct native target exactly, the lower output has
the same 4:3 content, and the inactive LCD is black. Original-scene rejection
still includes unknown 2D, transparent backdrop, polygon IDs and fog policies.
The component lab's explicit substitutions cannot enable original-game frames.

`SceneView` also carries owned native image placements and the 3D surface's
screen/order. The scene owner resolves logical images through its shared image
cache, pins every required bank before eviction, and sorts compact indices into
two bounded LCD spans. Reusable composition storage grows only when needed;
the observed 3D plus caption pair uses 256 CPU bytes. Warm frames upload neither
image pixels nor static compositor geometry. The title's typed producer now
supplies its lower-LCD route and live caption; [combined component evidence](NATIVE-MENU-2D.md)
retains explicit pixel-policy substitutions and complete-scene refusal.

This integration adds one full-screen draw and changes framebuffer payload
from the direct target's 768,000 bytes to 1,566,976 bytes: 1,048,576 in the
backend-owned colour/depth surface plus 518,400 in both RGB8 outputs. The
resident compositor mesh adds 256 linear bytes. Both categories are logged;
these figures exclude SDK metadata, allocator overhead, gfx buffers and the
rest of the application. This cost enables native layer composition and still
requires hardware measurement and optimization before a performance claim.
