# Native frame integration

`NativeFrame` connects the original loop to libctru input, the original GX
command stream, owned texture/video storage, and Citro3D. It does not advance
the game, replace its scenes, replay recorded poses, or supply a new camera.

The caller initializes libctru graphics and the game's data before constructing
and initializing this adapter. Allocate it statically or on the heap: its three
2D textures occupy 786,432 bytes before other members.

Call order:

1. `sample_input()` each display VBlank.
2. `begin_tick(error)` once at the start of the original loop iteration. This
   consumes latched input, writes the original communication records and opens
   the GX command buffer.
3. Keep the original touch/button samplers, then call `input_ready()`. This
   publishes a short press even when it started and ended between game ticks,
   and supplies Circle Pad input to the existing original CheckInput wrapper.
4. Run the original scene update/render and swap gate, then `end_tick(error)`.
5. At presentation call `present(error)`. Both eyes use the same original pose.
   The original runtime owns the VBlank divider; extra presentations never
   execute another simulation step.

The frame adapter uses the actual GX projection. It preserves vertical framing
and widens horizontal coverage to 400×240 instead of stretching the geometry.
Original 256×192 menus scale uniformly to 320×240, with 40-pixel side margins on
the upper screen. The original POWCNT display swap routes engines between the
panels. C-stick is sampled independently of the DS directional pad, but the
original camera bridge must consume `input().frame.camera` or install the
camera callback before camera functionality can be claimed.

`native_2d.cpp` adapts the first compositor block of upstream
`port/ntr/ppu_sub.cpp` at commit
`6230b2996636e3fd0e03cb178161c94cb4b73cc9`. It retains text, affine and bitmap
backgrounds, OAM sprites, priority, windows, blending and brightness. Memory
accesses use `native_video`; no DS bus address is dereferenced. The original
VBlank display commit in `native_display_commit.cpp` keeps its virtual call
and OAM upload order, translating only register accesses. These providers
replace original `func_02019144` and `func_020190b8` objects in the linked bundle.

Remaining rendering limits include multiple projection changes inside one
frame, blending between native 3D and 2D targets, GPU display capture, scanline
changes, mosaic and main-engine extended-palette bank variations. GX texture
coordinate modes 2/3 are still rejected. None of these limitations establishes
that an original scene has reached or completed that path.

Verification for this change: ARM11 cross compilation of the adapter, original
display commit adaptation, 2D compositor, input, renderer and GX decoder.
There was no new Azahar launch, recorded-pose replay or physical-hardware test.
This compilation is not evidence that the original menu or a playable game
has booted, nor a performance measurement.
