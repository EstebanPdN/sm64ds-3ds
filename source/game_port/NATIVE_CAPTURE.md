# Native display capture contract

`native_capture.cpp` writes the game's owned physical VRAM from explicit
engine-A image inputs. It does not render a frame, substitute a blank image,
or make a 3DS framebuffer count as the original DS graphics-engine output.

At the native equivalent of line 0, call
`native_capture_begin_frame(frame_serial)`. This snapshots `DISPCAPCNT` and the
destination's bank allocation. Its `needs_composited()` and `needs_3d_only()`
queries let the renderer request exactly the required readback. A disabled
capture or a destination outside LCDC needs no image. At the equivalent of
line 192, call `native_capture_complete_frame(request, sources)` once.

Each source contains 256x192 RGBA8 pixels, in byte order red, green, blue,
alpha, with the top row first and an explicit byte stride. The selected image
must have a nonzero `rendered_frame` matching the request's frame serial. An
allocated buffer with no completed-frame serial remains unavailable. The
caller owns the buffers until completion and must supply actual completed
engine-A output; a serial is a provenance contract, not an image-quality
detector. Unit-test patterns are identified as synthetic fixtures only.

The composited source is the BG/3D/OBJ graphics output before display-mode
selection and master brightness. The 3D-only source is separately available
even when BG0 does not display 3D. These tap positions are described by
[GBATEK's video system diagram](https://problemkaputt.de/gbatek.htm#dsvideodisplaysystemblockdiagram)
and the [BlocksDS capture tutorial](https://blocksds.skylyrac.net/tutorial/advanced/video_capture/).

The ordinary `Native2DFrame` applies master brightness and selects VRAM display
mode before presentation. Its underlay alone omits 3D. The separate
`native_2d_capture_layers` entry point now composes the original engine-A layers
while retaining `BLDCNT`, `BLDALPHA` and `BLDY`, omitting the display selector
and `MASTER_BRIGHT`. It supplies separate underlay/overlay and geometry state;
its underlay alone is still not a completed capture source.

`NativeFrame` and `GameRenderer` now integrate a dedicated 256x192 GPU target.
The renderer combines those capture layers with the decoded geometry, waits
for the completed GPU frame/display transfer, invalidates the readback cache,
and converts the 3DS transfer's column order and ABGR bytes to top-first RGBA8.
Only a successful draw/readback supplies a matching completed-frame serial to
this backend, which writes the physical VRAM. It does not read the stretched
400x240 presentation panel or substitute a blank source. The current frame
integration explicitly rejects 3D-only, source B and blending; forced blank
is an explicit failure in the capture tap. Supporting a supplied 3D-only
image in the backend does not mean such a GPU image is currently produced.

The four capture sizes crop the source's upper-left region and produce tightly
packed 16-bit bitmaps. A/B/C/D destinations must be enabled in LCDC mode;
offsets wrap within their 128-KiB physical bank. Capture completion clears bit
31. Source selection and color/alpha transfer follow
[GBATEK's capture specification](https://problemkaputt.de/gbatek.htm#dsvideocaptureandmainmemorydisplaymode).

The format is BGR555 with bit 15 derived from nonzero input alpha. Compounded
2D/3D output is not substituted for a requested 3D-only image. Source B and
A+B blending are explicit unsupported-source failures in this bounded backend.
EVA/EVB do not affect an A-only capture.

`Scene::ResetHardwareRegisters` in the pinned source writes `0x80000000` after
disabling banks. The capture specification requires an LCDC destination, and
[GBATEK's VRAM notes](https://problemkaputt.de/gbatek.htm#dsmemorycontrolvram)
give the capture unit access in Plain-CPU allocations. This request therefore
completes as `no_lcdc_destination`, writes zero bytes, requires no image, and
clears enable at the frame boundary. It cannot overwrite a texture/BG/OBJ bank.
This decision uses those documented access conditions; it does not inherit the
upstream port's stated uncertainty as evidence of physical behavior.

The pinned `port/ntr/ppu.cpp` captures only A and samples its host framebuffer,
sets alpha unconditionally, and declines offsets that exceed the bank. The
native backend instead distinguishes both A inputs, preserves alpha and
implements bank-local wrap. It uses the existing physical storage in
`native_video`; reassigning a captured bank exposes the same bytes through its
new alias, with no publishing copy.

The result distinguishes `idle`, `captured`, `no_lcdc_destination`, and
`failed`. Missing/incorrect source frames, source B/blend, malformed strides,
source/destination overlap, and mid-frame control/allocation changes set an
explicit platform failure and leave VRAM unchanged. Completion clears enable
on these failures as well. Scanline-dependent changes are outside this bounded
frame-level contract.

Run `python3 tools/test_native_capture.py` to compile and execute the host
contract test under ASan/UBSan and compile the backend for ARM11. The test uses
the real `native_video` store and controlled RGBA fixtures. It covers all 128
bank/size/offset/A-input combinations, wrapping, cropping, padded strides,
channel and alpha packing, whole-VRAM canaries, aliases after reassignment,
non-LCDC/reset requests, image availability, unsupported sources, invalid
metadata and start/completion enable timing. The recorded report is
`build/native-capture/report.json`.

These backend checks prove host memory/contract behavior and ARM11 compilation.
They run no Azahar cycle. GPU readback has a separate synthetic fixture report
in `build/native-capture-gpu-azahar/latest.json`: attempt
`1791594319051461000` passed under the Old-profile emulator with two 196608-byte
RGBA8 images, one with layers and one with layers plus two geometry triangles.
Both actual/expected images were byte-identical, with zero differing bytes.
That fixture executed no game code and verified no physical hardware or FPS.
The original game's real capture request had not been reached in the reported
game attempt; this document does not declare that menu/game capture passed.

## Original menu control flow

The request is needed during the transition from the interactive title to the
first main-menu state, before choosing or starting a saved game. It is not an
initial `DSMTInitResources` request. The following bounded trace follows
original bodies and the supplied ROM:

1. `func_ov007_020b6544` initializes manager `+0x18` to zero and pending graphics
   `+0x1c` to mode 1. The first graph callback therefore does not arm capture.
2. In title substate 0, `func_ov007_020b1cf0` accepts a fresh touch or a fresh
   B edge when the title element is fully scaled and its input gate is clear.
   It enters substate 4 with transition mode 1 for touch or 0 for B.
3. `func_ov007_020b1718` completes the touch-animation condition, or the B-path
   counter condition, and requests main state 1 and graphics mode 2. That
   graph callback releases D to LCDC (`VRAMCNT_D=0x80`) and assigns E to BG
   (`VRAMCNT_E=0x81`).
4. `func_ov007_020aed98` advances the original state objects, initializes
   substate 0, and dispatches `func_ov007_020b0358`. Its frame-zero case sets
   manager `+0x18=1`. With the render gate set, `func_ov007_020b6eb4` calls
   `func_ov007_020bd4e8`, which writes exact `DISPCAPCNT=0x80370010`, then
   clears the manager request. This selects composited engine A, bank D,
   write offset `0x8000`, and size 256x192. EVA=16 is unused for A-only mode.
5. The next substate-zero frame requests graphics mode 3. Its graph callback
   changes D to main BG (`0x81`) and `BG3CNT` to `0x4285`. The direct-colour
   bitmap's base is `0x8000`, matching the captured region. E returns to LCDC.
   Original saved-file result codes and `StartFile` occur after this transition.

`tools/audit_native_capture_flow.py` executes the control/fader/bank leaves
with constructed state/register fixtures. Both fresh-touch and fresh-B cases
passed; five other substate/counter guards left the capture flag clear. At the
capture write, both traces have `DISPCNT_A=0x00011b08`,
`DISPCNT_B=0x00011c00`, and `MASTER_BRIGHT_A/B=0`. Completed original fade
request fixtures (`4/6`, `7/0x18`, `3/0x0a`) leave `BLDCNT_A=0x00bf` and
`BLDY_A=0`. Those blend values are the result of the tested completed fades,
not a claim that every animation/input timing yields identical values. The
tap must retain blending even when it omits master brightness/display mode.

The report is `build/native-capture-flow/report.json`. It declares every
intercepted rendering/resource operation; those operations do not run here.
The CPU fixture records register writes and explicitly supplies only the
auto-cleared control state needed for the following step. It produces no
capture pixels and proves no complete scene, gameplay, emulator application,
physical console or timing. Its direct-call static audit also excludes
indirect calls/modules outside ARM9 and OV007. The profile and GX arithmetic
MMIO blockers found by actual game attempts remain independent evidence;
absence of a named STOP in the static graph does not prove a service is native.
