# Original HUD, Old3DS stylus camera, initial Options

User priority2026-10-18: stop the physics/performance campaign and complete
these corrections first. The prior candidate and evidence remain preserved;
no additional physics changes are part of this revision.

- Restore the original ROM's lower HUD graphics/palettes and ordinary object
  rendering, retaining maps/castle backgrounds. Do not use the rejected traced
  portrait atlas or newly drawn counter/camera graphics. Native pause/options
  artwork is retained separately.
- Remove original lower camera-arrow/zoom graphics and their invisible pen
  hitboxes. Physical camera controls remain in the original input mapping.
- Detect Old3DS through APT; in ordinary live gameplay, stylus displacement
  controls the same free yaw/pitch orbit as C-Stick. First contact establishes
  a baseline, stationary contact does not rotate, release stops movement,
  recontact does not jump, delta accumulation handles HID/game cadence, and
  menus/dialogues retain their original pen input. A single sensitivity applies
  to both axes. New3DS keeps its actual C-Stick behavior.
- Original title wrench element7 routes its validated selection callback into
  the shared Options model, with Controller/Audio/Display/Developer/Update.
  Buttons/Control and sensitivities are accessible before starting a save.
  Preview/cancel/accept/save semantics remain transactional; pen/keys used to
  close the panel cannot activate the underlying menu. Original menu context
  and registers are not rewritten as a fake Stage pause.
- Front Options use the original opaque blue Mario DS pattern across the
  entire lower screen, with native option buttons above it. This covers the
  title face/buttons only while that modal is open. In-game pause retains its
  transparent background over the current level/map.

Sources reviewed: Super-Mario-64-HDS is a16xDS-inspired pack for the N64 PC
port, not an installable SM64DS3DSHUD replacement:
https://github.com/allnought/Super-Mario-64-HDS
RenderDS is likewise a PC/Remastered texture project:
https://github.com/crypticalwastaken/RenderDS
Use the user's supplied ROM assets for the faithful default.

Implementation: prepare_integrated_native_ui.py excludes replacement HUD
atlases from the viewer; prepare_integrated_camera.py suppresses retail camera
buttons and their touch regions; stylus_camera.hpp and cstick_camera.cpp share
the free camera orbit; native_pause_menu.cpp and prepare_integrated_native_pause.py
route the actual wrench event, without global coordinate shortcuts.

Host validation: stylus first-contact/accumulation/consume/release/recontact/
clamp/reset under ASan/UBSan; shared Menu general root exposes Controller and
preserves cancel rollback and change-only save requests; guarded original source
hook tests include the wrench and still reject vendor/symlink/partial writes.
Build and live UI verification remain pending at this initial record.

Prior physics run attempt1792346992238096000 was closed before this revision.
Its raw captures remain pending analysis; they are not silently deleted or used
as acceptance evidence for these UI changes.


First live UI candidate c6efa1e170a1e6ca opens the shared root from the actual
wrench and enters Controller. That test exposed a second retail SDK pen sampler
which bypasses the ordinary filtered input: the underlying menu changed to the
picture editor. This candidate is not accepted as a complete modal fix.
The corrected source suppresses the SDK sampler through panel ownership plus
pen-release gating and bypasses the retail menu update pump while blocked.
Its guarded hook tests pass; rebuilt/live return acceptance remains pending.


Second live candidate4b96af3410793f11 confirms native root/Controller access,
but closing still exposed a queued retail face transition. Source-level cause:
the intercepted func020b63e4selection returned before the original
func020c1d8c(0) consumed-pen latch. The final source now sets that original latch
before returning; the remaining elements cannot select the same pen press in
the same frame. This is separate from blocking later SDK samples and update
pumps. Do not classify either earlier candidate as full return acceptance.


Candidate95f2bc484896ada1 builds and passes host/source/packed ABI gates.
Ordinary Old-model UI attempt1792348965004523000 confirms the original wrench
opens Controller/Audio/Display over the opaque original blue Mario DS pattern.
Entered Controller, returned with B, then closed Options: original face, wrench,
VS/Adventure/Rec Room are restored. Saved before/after screenshots and observations.
This focused UI check does not certify the gameplay stylus path or physical hardware.
