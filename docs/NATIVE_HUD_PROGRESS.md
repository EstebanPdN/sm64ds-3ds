# Native lower HUD work

> **Superseded (2026-10-24).** The repainted native HUD described below
> (`native_hud_icons*`, `native_counter_*`, `native_camera_hud/gpu/identity`,
> `native_hud_art/painter`) was never wired into the game and has been removed.
> The interface is now drawn from the original DS sprites at integer 1x:
> `sprite_layout.hpp` + `native_sprite_overlay.hpp` (lower-screen sprites) and
> `ui_layout.hpp` (upper-screen 2D elements over the native 3D); the camera
> buttons use `native_gameplay_camera.hpp`. See STATUS.md. The notes below are
> kept as history.

Status: all12camera template variants are integrated (large/small left/right/
zoom, normal/pressed). Normal/pressed Standard and Touch zoom have exact live
pixel proof. Dual-hand small layout/basicCirclePad is observed; remaining HUD,
full effects/scene coverage and physical acceptance remain pending.

`tools/generate_native_hud_art.py` authors14assets with integer pixel masks,
contours and bevel rules; no resampling or antialiasing. Camera frames40×60
match the native extent of the original32×48DS frames. Transparent arrows,
normal/pressed directions, star20×20 and multiply12×12 are included.
Editable generator emits PNGs, palette-index C++ data and a manifest.
`native_hud_painter.hpp` copies1:1 with320×240clipping, no runtime resizing.
Header compiles; PNG dimensions and binary transparency were checked.

`build/native-hud/captured-obj-1792336381684001000/identity-analysis.json`
records25sprites decoded through the pinned original ObjPlan/decode code.
Original user's capture is not modified. Exact original ELF.text matches; only
fully stored OAM/palette/OBJ VRAM ranges were read from its incomplete prefix.
Decoder built/executed with ASan/UBSan. The private atlas includes originals,
so it is diagnostic material, not a release asset.

Important integration finding: each camera button is four original objects:
32×32top,32×16bottom,16×16main arrow and8×16tail. Four such groups exist.
Replacing an arbitrary OAM index or painting a new button over the old image
would preserve mis-scaled remnants or affect hidden controls. Match complete
geometry and immutable encoded/palette identities, route only eligible
gameplay state, and honor priority/brightness before suppressing old sprites.
Keep unknown/affine sprites, portraits and original maps until their proper
native replacements are complete.

Pending: finish portraits/digits/markers, broaden hidden/effect/other-scene
coverage and validate remaining native HUD output/touch on physical devices.

## Integrated normal camera buttons (2026-10-18)

`native_camera_hud.hpp` matches exact palette/encoded bytes, unflipped geometry
and complete4-piece camera groups. It declines partial/unknown groups, ahead
BG/OBJ overlaps, wrapped affine occluders and unsupported effects/windows.
No OAM/VRAM writes or slot-only identities. `native_camera_gpu.hpp` prepares a
native320×240transparent image on the renderer owner thread and publishes the
old-object skip mask only after its GPU texture is ready. Map interiors remain
transparent. Original inputs/hit rectangles and all non-camera sprites remain.
Texture512×256RGBA8 uses524288linear bytes, CPU image307200ordinary bytes and
one6-vertex quad; image rebuild/upload occurs only on visible group changes.
These are bounded owned resources, released through renderer shutdown; this
is not a whole-game leak/low-memory hardware acceptance claim.

`test_native_camera_hud.cpp` checks the25captured OBJ plan,4groups/16pieces,
missing/shifted/flipped/changed-palette inputs, priority/overlay/OBJ occlusion
and malformed order under ASan/UBSan. Complete candidates build/audit.

Oldcastle/Yoshi candidate21c4eeb0287b164c,
attempt1792337158519119000:3complete RAM captures,4native groups each, exact
CPU image and tiled texture.25old OBJ draws become9plus1native quad. Pause
retains original upper statistics over the held level; Continue resumes with
map/buttons. These were observed through CUA, not saved standalone screenshots.
The legacy `.render` framebuffer was discovered to show dump_progress, so its
opaque-pixel comparison is explicitly not game-target proof. Its original
analysis is retained as discovery evidence, not relabeled as a passing final
pixel check.

NewBob/Mario candidate754d93d2e5563e12
(ELF754d93d2e5563e12b911b7d90c48fd00beebce4bdf460d2f925c38e8c1864217),
attempt1792337750669614000: original introductory dialogue advanced with real
A input, Mario body/control validated, movement/jump performed,3complete RAM
captures and allocator lists pass. All3snapshots have4groups;3712opaque
pixels match frozen native art exactly in both CPU/GPU texture and final
`.lower` output. `native-camera-hud-analysis.json` retains proof.

The new `.lower` companion is a307216-byte explicitly sized game-target
readback, distinct from the preserved legacy `.render` LCD/progress image.
Both resident and fallback blit destinations are tracked; invalid/released
targets are refused. Host diagnostic tests cover header/payload and failed
readback; Delete Dumps recognizes only correctly named `.lower` companions.

Runtime matching now uses exact memcmp rather than per-byte64-bit hashes.
Observed native-HUD preparation/draw means: Oldcastle0.638/0.097ms before
that change, NewBob0.327/0.070ms afterward. Different scenes/profiles/input
windows prevent treating those numbers as a controlled speedup measurement.
All6new rawVM files analyzed/retired with hash receipts; reports/trace/anchors/
render/lower files and user originals remain. Final campaign and physical FPS
are still open.

## All camera states and original button modes (2026-10-18)

The original decomp `actors/dMeter_c.cpp::RenderCameraButtons` selects12OAM
templates: large/small left, right and zoom, normal/pressed. Press feedback
changes the arrow from red to gold; the frame does not sink.

`tools/replay_camera_oam.py` executes the exact captured ARM RenderSub in an
offline copy of the original RAM, after matching ELF.text. It uses only fully
captured readable mappings. Fresh staging counters/template/position parameters
are controlled offline; the input capture is untouched and no live game runs.
`camera-states-1792338386566327000` records all12variants. Their OBJ plans were
decoded through original code under ASan/UBSan.13unique byte/palette identities
are generated by `generate_native_camera_identity.py`; all12groups and stale
pressed-image prevention pass host tests. Receipt:
`build/native-hud/all-states-gate-1792339759095238000/report.json`.

The generator now provides22assets. Large frames40×60, small40×40, with
transparent interiors; zoom contour uses the original glyph's proportional
height. Group metadata preserves its28-byte diagnostic layout with action,
pressed, small and piece count in the former padding. Zoom retires3pieces,
directions4. Native image caching includes action/size/press state. Failed
CPU image allocation releases GPU resources and latches safe original fallback
rather than retrying every update.

Candidateebc57e0e12111ceb
(ELFebc57e0e12111cebffbc40cd2dd11bd90128fafdc9cbcfe92e0c9955178d0128,
imageaa21912ff3606952a9bd6fb4ed61ae400c815518a8228dbf27763947400d47c9)
builds/audits. Old attempt1792339119063382000 closes its3capture run without a
logged fault after321.085seconds including post-capture UI inspection. Real
profile-local held pen C atDS25,165 rotates the camera; R atDS231,165 changes
zoom. Native pause/Controller/Buttons Touch and Dual-hand selections/OK and
Continue were exercised with real200ms pen input. Rapid repeated keyboard G
presses missed release edges; those are not claimed as working navigation.

Actual capture states: Standard normal(3712opaque pixels), Standard with both
left arrows pressed/gold(3712), Touch normal zoom(944). All match CPU image,
tiled GPU upload and synchronized final `.lower` exactly with zero differing
pixels. Dual-hand small left/right/zoom and CirclePad movement were observed
after the3captures; its attempted extra dump was rejected after supervisor
closure, so no extra RAM proof is claimed. Native mode changes are original DS
button modes; independent Original/Hybrid/N64 physics integration remains open.

Campaign/allocator analyses pass; only these3new rawVM files were retired,
retaining reports/trace/anchors/lower images and original user captures.
`camera-state-comparison.png` shows the3captured states without scaling.
There is1distinct sampled character position in this UI-state series: it is
not a multi-zone performance run or any completed final ten-scenario matrix.


## Native counter contours and main motion integration (2026-10-18)

`generate_native_counter_hud.py` paints13counter assets at native dimensions,
without bitmap resizing: ten10x20numerals plus20x20multiply/star/coin. Original
RenderSub-derived byte/palette identities gate their replacement. The shared
native camera image now includes counters; unchanged combinations retain the
image/GPU upload and only admitted original OBJ pieces are suppressed. Unknown
sprites, priorities, windows/brightness effects keep original rendering. The
original map remains the background. Portraits/map markers are still pending.
`test_native_counter_hud.cpp` checks13real captured templates and synthetic
frozen identities under ASan/UBSan; malformed palettes/bytes/order, affine/flips,
priority occlusion and image-key changes are exercised. It is also a main-build
gate. NewBob candidate0f8a75437d97cd8b completes3captures; its physical/remaining
HUD acceptance is not implied by that completion.

Main `native_motion.cpp` deliberately excludes laboratory autoplay, forced
castle returns, teleports, fixture character/save logic and lab UI. Its derived
behavior seam keeps one displacement owner and original actor interactions.
Actual Oldcastle RAM in attempt1792341510703511000 proves both Hybrid and N64
ownership without latched faults. Five captures analyzed/retired; retained
motion-analysis.json records mode/solver/action/position. The initial production
attempt exposed an idle/wake mixer hang, fixed with initialization guards and
an1800-idle-update wake regression. Bob's next attempt correctly fell back to
DS after duplicate moving-collider aliases failed readiness; canonical alias
handling is now validated offline and awaiting the next live result. This is
not evidence of complete physics parity or stable physical60FPS.

## Production HUD atlas and independent physics (2026-10-18, candidate0d4b19cc02ea82c5)

The main game now links the DS-world N64 solver. Controller/Buttons remains
Standard, Touch, Dual-hand; Controller/Control independently selects Original,
Hybrid or Nintendo64. The core reads original logical DS commands, preserving
jump/attack/crouch mappings. Analog CirclePad removes only its synthesized
high-bit digital directions during gameplay, retaining physical Dpad and menu
navigation. Hybrid Luigi/Yoshi held jumps and original attacks, grabs,
transformations, water/hazards remain explicit DS ownership handoffs. Native
ordinary movement uses one displacement owner, with original platform/contact
identity and staged failure. No laboratory autoplay or fixture teleports enter
production. This is integration evidence, not all-interaction parity.

Thirty portrait/map/cap/marker templates join the13counter glyphs and12camera
templates. `generate_native_hud_icons.py` traces original colour-region edges
onto final integer grids; it does not invent new portrait detail. A resident
512x256RGBA8atlas costs524288linear bytes; moving icons change quads, and the
rotating arrow updates only its16KiBreserved tile. The staging image is released
after initialization. Counter/camera images share their dirty cache. Exact
original byte/palette/group/priority admission preserves original fallback for
unknown, windowed, clipped or conflicting states; original map backgrounds
remain unchanged. Cap composites are admitted before bare heads. The icon
layer excludes pieces already owned by the counter layer, fixing the shared
small-star identity collision.

All25original pieces in the saved normal-castle fixture are accounted for by
16camera pieces,5counter pieces and4portrait/arrow pieces. ASan/UBSan gates
cover the actual templates, malformed bytes, priorities, visibility and atlas
placement. In NewWhomp attempt1792345478294685000 all3complete captures have
zero differing final opaque pixels in both HUD inspectors, with exact frozen
CPU/GPU art and icon quads. These captures precede the later Hybrid selection
and therefore are Original physics evidence only. All3VMs were analyzed then
retired; reports, game-target readbacks and hashes remain. Earlier failed
visual runs are retained as failed evidence, not counted as acceptance.

Bob native acceptance was subsequently captured in attempt1792342816985304000:
all4RAM samples show real Nintendo64 ownership and finite solver state, with
no latched fault. Canonical moving-collider alias validation and expected scene
registry refresh fixed the original failures. Whomp exposed one exact
zero-determinant prism: host FMA rounding hid the degeneracy, ARM did not.
Exact integer classification now retains it in the516entrywall cache; nearby
contact explicitly delegates to the original DS response, while far queries
continue natively. Original-ARM replay and fast/off-FMA sanitizer cases pass.
Physical performance, all characters/interactions and uncommon HUD states
remain distinct acceptance work.

### Old Whomp Hybrid/Dual-hand acceptance

Latest unchanged executable0d4b19cc02ea82c5, attempt1792345984308209000:
fresh real persisted settings Hybrid/Dual-hand, original Luigi body and Whomp
level verified by the scenario. CirclePad and actual DS X jump were exercised
with real UI input, including a sustained Hybrid jump, landing on the ledge
and another jump/return. Three scheduled captures plus one later L+R+A capture
all prove native Hybrid displacement ownership, finite positions and no
latched fault (steps399,919,1726,2949). This closes the earlier Whomp readiness
failure in actual main-game execution. It does not prove every nearby prism
or all DS interactions. All4camera/counter and icon readbacks match their
frozen pixels exactly; allocator reciprocal/linear/VRAM checks pass. All4new
rawVMs(457347264bytes) analyzed/retired, receipts and readbacks retained.

### Clock/Wario diagnosis and conservative ceiling admission

Candidate0d4b19cc02ea82c5 attempt1792346191134813000 is a failed native-physics
acceptance run, despite four complete captures and intact HUD/allocator checks.
All4RAM samples select Nintendo64 but delegate to DS; no solver steps occur.
Original-ARM replay of saved RAM (`clock-contact-replay.json`) proves the floor
atY=-4710 is ordinary CLPS0x32005/class2/effect0. The ceiling atY=-2630 has
CLPS0x8b000c/effect17: a2080unit separation. Generic surface-description refusal
was vetoing native movement for this distant ceiling, causing repeated solver
creation/static-prism rebuilds. This is not accepted as native performance.
All4new VM files analyzed/retired; metadata/contact/HUD/allocator reports remain.

The source now retains/revalidates the scene wall cache across temporary DS
ownership, resets contact/platform handles only after deleting the solver, and
fully invalidates on scene/character/fault teardown. Replaced registries rebuild
instead of reusing stale geometry. Expected stationary terrain delegation has
a15updatebounded retry, with immediate retry on changed position/state/material
or new logical action; DS simulation continues throughout. Sanitizer gate
checks1600stationary updates make100attempts, with all change/action/clear paths.

The original ceiling query still runs every step. Ordinary ceilings are always
retained; unsupported ceilings farther than queryY+160(the imported engine's
body collision height) are returned as missing until reachable. Unsupported
nearby ceilings retain original DS delegation, and floor/material rules are
unchanged. Staged output remains untouched on a suppressed distant ceiling.
Actual Clock CLPS and exact160unit/oneFix12boundary plus integer extremes pass
the sanitizer gate. A new main candidate is building for live verification;
this source change is not yet a claimed native Wario result.

### Live Clock correction verified (candidate0dba8754543e806a)

Attempt1792346749071624000 confirms actual Nintendo64 ownership for Wario in
two complete process captures:399and955native steps, finite position, no
latched fault. One reset initializes the scene, replacing the prior per-update
terrain resets. Original Touch buttons remain independent. Real CirclePad
Up600ms/B500ms input then moved Wario off the entry ledge; the original actor/
stage transition ended the scenario. Its third complete capture is a terminal
transition diagnostic with no player, not a third steady performance sample.
The interactive analyzer now admits this absent actor only for a verified
explicit stage-character-changed diagnostic and excludes it from gameplay
counts. No final campaign/three-sample claim is made. All native opaque pixels
in both live actor captures match, allocator lists pass in all3captures; all3
new rawVM files(431103120bytes) analyzed/retired with receipts. Reports retain
this failed campaign boundary rather than fabricating a stable third sample.
