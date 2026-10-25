# Current acceptance checklist

Current consolidated inventory (2026-10-21):
[Inventario del port y lista de trabajo](INVENTARIO_PORT_2026-10-21.md).
It reconciles the historical entries below with the current source and separates
implementation, recorded acceptance and remaining verification.

Current request, 2026-10-23: development and performance work resumed. Focus on
Old 3DS, complete diagnostic bundles and Display > Lock FPS (Off/30/60; Old
defaults to30, New to60). The physical target remains stable60 on both models.
See [current dump evidence and limits](NATIVE_RAM_DIAGNOSTICS.md) and
[the corrected timing interpretation](CODE_PASS_2026-10-22.md).

- [x] Candidate c2451eea built/audited; Old-model Azahar run closed with three
  timed and three directed dumps. All six include complete6MiBVRAM and valid
  top/bottom LCD scanout, with matching context and closed-receipt hashes.
- [x] Actual Display page inspected: default30, selection60, reopen retaining60,
  selectionOff, save/resume and ordinary-HID dumps. Bottom LCD captured before
  the progress page; UI screenshot files preserved in the acquisition.
- [x] Final candidate6788c4dd includes cadence-aware automatic dumps. Old/30
  reference closes in97.35s with3complete LCD/VM/VRAM bundles and0automatic
  captures; detector regression distinguishes ordinary33ms from real stalls.
- [ ] Physical acceptance, complete routes, broad interpolation regression and
  valid per-function CPU sampling remain open. These checks do not close them.

Historical note, 2026-10-18 (superseded by the current request): physics/performance
work was paused to prioritize HUD/camera/Options. The historical entries below
retain their original candidates and evidence; they are not current acceptance.

## Latest request — distinguish submenu actions from changing values

- [x] Options categories now use full orange TOUCH TO START-style action buttons, with category names inside and no separate OPEN controls. Python copies the original240x34 bevel at1x and extends only the clean interior. Existing value selectors retain their cream frames and green arrows.
- [x] Developer now reads CLEAN DUMPS with an orange CLEAN action.
- [x] Follow-up: Dump RAM now has original green arrows to toggle capture permission. They never request a capture; A/the centre retains the capture action. Enabled value is L+R+A, with Python-authored7x7 plus sprites on the original1x pixel grid. Full Dumps mirrors the same existing permission flag.
- [x] Confirmation displays DELETE DUMPS? and horizontal KEEP / DELETE actions. Removed redundant OK; Back cancels. Shared geometry governs drawing and touch. Left/right chooses without activating; A confirms. Regression covers both actions, Back, gaps and retired hit boxes without deleting files.
- [x] Native model and bounded artwork tests pass ASan/UBSan; artwork and all-page render compile with Wall/Wextra/Werror. Reviewed four changed pages at native320x240 and integer enlargement. Source previews: assets/proposals/options-action-buttons-2026-10-18.
- [x] Candidate fbdc5381c5b977da compiled/audited and ran in New-model Azahar for187.6s, no recorded emulator fault. Actual full Options pages, CLEAN question, inert gap, KEEP/Back return, left-half category touch and OK/title/wrench reopening observed; screenshots saved beside the source previews.
- [x] Follow-up candidate a2a557f5b08a5050 compiled/audited. New-model Azahar attempt1792358115954381000 closed normally after174.6s with no recorded emulator fault. Right arrow enabled L+R+A, left arrow restored Disabled, neither opened a dump screen; CLEAN/KEEP/Back/OK and wrench reopening observed. Six current full screenshots, unchanged lower-LCD contact sheets and hashes saved in assets/proposals/options-action-buttons-2026-10-18.
- [ ] Physical-console acceptance and intermittent upper-title display regression remain open. Keyboard-right inspection of the preceding candidate overlapped the profile's keyboard-touch mapping; horizontal choice behavior is covered by the host model, not claimed as a physical button test.

## Correction after rejected star/font/paging review

- [x] Source black crops were importing neighbour strokes into glyphs (L beside I). Rebuilt every outline exclusively from its own ink silhouette. All26glyphs have one connected visible silhouette; no detached black marks.
- [x] Original11x12 selection star returned to1x. One original22x15 blue down arrow centered below compact rows cycles pages; green arrows remain beside changing values. Touch geometry updated and tested with ASan/UBSan.
- [x] Both HUD arrows use complete original32x32 frames at1x, replacing fractional40x40 review assets. New visual artifact E-original-HUD-integer-arrows.
- [x] Candidate b097c2802a1c00b0 built and audited. New-model Azahar attempt1792356750522337000 opened all12menu views, exercised blue paging, volume touch100to155, OK/title return and wrench reopening; no recorded emulator fault at bounded close. Actual screenshots and contact sheets in assets/proposals/options-clean-pages-2026-10-18.
- [ ] Physical-console UI acceptance remains open. Upper title scene rendered intermittently during options navigation; retain display regression. One automated drag gesture did not alter volume although direct touch worked; do not mark full hardware drag acceptance complete.

## Original menu controls and integer pixels

- [x] Rechecked Mario Universe Star sheets and TSR Menu Elements6855. Selected the original flat menu selection star from the clean magenta sheet; preserved its black outline by removing only the exact magenta palette entry.
- [x] All visible menu controls now use original1x pixels:64x32 complete OK,10x14 green arrows (right mirrored),32x32 back. Selection star uses exact2x blocks (11x12 to22x24). Fixed labels at x44, star at x12 and consistent10-pixel gap. Frames extend only flat interior, and value font uses1x pixels.
- [x] Pixel audit compares each rendered control pixel to its source1x or2x block; passes. Native model and all-page drawing pass ASan/UBSan; every review page regenerated. Evidence:assets/ui/retail-pause/pixel-controls-audit.json.
- [ ] Actual emulator/console acceptance remains pending; generated review pages are not emulator captures.

## Retail label proportions correction

- [x] Replaced the tall 7x12 letter plan with original measured 8x11 faces, 14-pixel raster height, overlapping black outlines and the original white/yellow/gold/salmon palette. Extracted connected ink components preserve transparent counters rather than filling enclosed background areas. Retail BACKLIGHT/SOUND supply 14 original glyph shapes; 12 missing letters are extended on the same grid in Python.
- [x] Regenerated every menu preview; ASan/UBSan all-page rendering and bounds checks pass. Compact horizontal volume remains unchanged.
- [ ] Visual approval and emulator/console acceptance remain open; complete alphabet extensions are not claimed as original ripped glyphs.

## Label font and compact volume revision

- [x] Replaced sliced word fragments and reduced title glyphs with a complete Python-authored outlined A-Z pixel alphabet. Fixed cap height, stems, baseline and shadow; original SOUND/BACKLIGHT artwork remains the visual reference. This is a recreation, not a verified pixel-identical original font.
- [x] Volume now occupies a 40-pixel-high single row with percentage on the right. Touch drag bounds and behavioral regression updated; ASan/UBSan model and all-page render pass.
- [ ] User visual acceptance and actual-console verification of this font revision remain pending.

## Options visual revision — retail title options reference

- [x] Shared options painter now separates outlined option labels from value buttons; retains blue Mario DS pattern without the Mario head. Developer toggles use separate ON/OFF buttons, current selection shaded. Original chamfered golden OK sprite replaces the rectangular footer.
- [x] Touch regions updated for relocated paging, sound arrows, volume drag and idempotent ON/OFF selection. Native menu behavioral regression and bounded render pass with ASan/UBSan.
- [x] Regenerated all 12 review views in assets/proposals/options-all-2026-10-18. These are rendered source previews, not emulator captures.
- [ ] Compile and verify revised options on emulator and hardware; keep all earlier functional issues open.

## Latest priority — original lower HUD proposals

- [x] User approved the chamfered SURROUND-style menu frames on 2026-10-18; retain this design.
- [x] Download original HUD & Minimap Stuff sheet 137713 and generate three 320x240 layouts from original sprites, with original castle map and two square left/right camera buttons. Source/crops/positions and reproducible Python generator in assets/proposals/lower-hud-2026-10-18. These are layout previews, not emulator screenshots.
- [x] User selected original-size A HUD with 40x40 B camera arrows; saved as D-approved-A-hud-B-arrows.
- [ ] Integrate selected original sprites and matching two-button touch regions, retain Old3DS stylus camera, then verify against actual dynamic HUD/game states. This supersedes the earlier request to remove every camera arrow.

## Regressions reported 2026-10-18 — active priority

These reports reopen acceptance; earlier build or screenshot checks do not close them. Latest weekly usage reading: 39% (owner cap: 50%).

- [x] Reproduce title Options open/close/reopen failure on delivered candidate95f2bc484896ada1, New-model Azahar. Wrench opens once, B closes, second wrench touch does not reopen. Original consumed-input latch was never released by the intercepted page.
- [x] Implement generated-source latch release plus neutral key/pen history on modal exit. Five-cycle executable C regression and six hook tests pass; candidate84c0fd998000d7ce builds and audits. Candidate6ff53495d95cd557 also records four modal opens/returns; Adventure and File A touch navigation worked after closing Options. Exhaustive rapid-cycle acceptance remains pending.
- [x] Generate SURROUND-style chamfered gold frames from the exact user-supplied sprite reference in Python; preserve corner pixels, use uppercase original bitmap labels, and enlarge the volume frame without repeating internal borders. Shared title/in-game painter updated; sanitizer rendering test passes. Candidate86acf4cfb28b7b59 built and passed integrated audit. Preview uses the actual shared menu painter on the original tiled background; live emulator/physical visual acceptance remains pending.
- [ ] Restore approved original menu appearance consistently at title and in-game, including original title, frames, selection star and back arrow proportions.
- [ ] Verify repeated title Options open/change/OK/back/reopen and Adventure/file touchscreen navigation without slowdowns.
- [ ] Remove OK flicker and apply display settings while paused without losing the retained level or original map/statistics.
- [ ] Preserve upper 2D sprite aspect ratio in Wide; avoid stretching the upper menu graphics.
- [ ] Native rendering from initial startup and opening cutscene; inspect actual targets and fallback reasons, not just saved labels.
- [ ] Diagnose and correct persistent physical-console audio crackling; distinguish starvation, source discontinuity and gain clipping.
- [ ] Opening intro: first Start shows “Start to omit”; release then second Start ends the opening via original APIs and uses the original white scene transition into playable Yoshi. FSM sanitizer test and candidate6ff53495d95cd557 build pass. In Azahar, first Start showed the hint, second reached playable Yoshi, and movement worked. Fade still needs polish: actors disappear before the white transition fully covers them.
- [ ] Reopen Original/Hybrid/Nintendo64 behavioral acceptance: the user reports only Original works correctly. Specifically reproduce broken triple-jump chaining, stuck locomotion and missing ground dust in Hybrid/Nintendo64. Check movement, animation, particles, collisions and DS abilities; solver ownership alone is not correctness.

## Current user priority

See HUD_CAMERA_FRONT_OPTIONS_REVISION.md. The previously drawn gameplay HUD was rejected; its historic pixel comparisons describe that old implementation, not the desired appearance. Restore original ROM sprites, remove camera arrows, add Old3DS drag camera, and expose shared settings from the original title wrench. Broad physics/performance testing remains paused.

## Completed implementation and inspected evidence

- [x] Clock/Wario failure diagnosed in original ARM saved-RAM replay: ordinary class2 floor, effect17 ceiling2080units overhead. Reachable unsupported ceilings still delegate to DS; distant effects no longer veto native motion. Scene wall cache survives/revalidates temporary DS handoffs; stationary expected refusals retry at bounded intervals with immediate action/state/position retries. Boundary/overflow/retry sanitizer gates pass; candidate0dba8754543e806a builds/audits. Two Clock/Wario captures prove real Nintendo64 ownership,399/955steps, no fault, exact native HUD pixels. Later input left the ledge and triggered original stage transition; third capture is diagnostic only, not a three-sample performance pass. All3newVMs analyzed/retired.

- [x] Main candidate0d4b19cc02ea82c5 builds/audits with independent persisted physics/button modes, actual DS logical commands, CirclePad digital-direction filtering and no lab autoplay. Bob Nintendo64 ownership was captured in4real RAM samples. Original interactions/abilities remain explicit DS handoffs rather than simulated button remaps.
- [x] Main lower HUD now includes12camera templates,13counter glyphs and30portrait/map/cap/marker templates, resident native atlas and correct cross-layer/priority ownership. All25normal-castle fixture pieces covered; NewWhomp3captures show zero differing final native pixels in both HUD inspectors. Original map/castle backgrounds retained. Unknown/effect states retain guarded original fallback; broad/hardware acceptance remains open.
- [x] Whomp exact degenerate prism diagnosed from original ARM RAM replay: integer determinant classification retains all516wall entries and delegates only nearby ambiguous contact to DS. Full-cache original-ARM regression plus FMA fast/off sanitizer tests pass; OldWhomp Hybrid/Dual-hand completes4real RAM captures with native owner true and no fault; native pixels/allocator checks pass, all4newVMs analyzed/retired.

- [x] Production integration now links the real N64 controller/DS-world backend. Controller has independent Buttons and Control submenus; Original/Hybrid/Nintendo64 persists under input.physics without changing DS button mode. Candidate236b32fb0bef4c9b Old castle run boots/resumes both native modes; two extra RAM captures prove Hybrid/N64 own displacement, finite state, no latched native fault. Five captures analyzed and retired. Yoshi held-jump/action exercised; broader characters/interactions/levels remain acceptance work.
- [x] Diagnosed real idle/wake hang with Azahar Wait Tree PC0x39880c: stop_sound walked uninitialized N64 mixer lists. Physics-only core now guards stop operations like play_sound. 1800-idle-update wake/walk/jump regression and420 original-ARM-backed wall/floor solver steps pass. Expected DS effects/unknown friction refuse a staged native tick without changing caller output; session closes/reset cleanly.
- [x] Added13native counter assets: ten10x20numbers and20x20coin/star/multiply, explicit Python integer contours and1:1painting in shared cached native HUD overlay. Exact original encoded/palette admission, priority/occlusion checks, invalidation and clipping pass ASan/UBSan, including actual captured original OAM fixtures. Candidate0f8a75437d97cd8b New Bob run completes3captures; final pixel audit pending at this update. Portraits/map markers and other HUD states remain open.
- [x] Bob scene readiness failure was traced to two exact duplicated moving-collider registry pointers, not guessed capacity. Canonical entries contain953triangles total. The adapter now validates both alias/canonical live identity, enumerates geometry once, and retains original-query contact validity. Three controlled offline original-ARM alias/stale-owner checks pass with16376query calls. Candidate7ab8ea163ead9dcb built/audited; live Bob acceptance in progress.

- [x] All12camera templates now recognized (left/right/zoom, large/small, normal/pressed) from original RenderSub executed in offline saved RAM.13unique exact byte identities; pressed gold arrows preserve the frame, small controls40×40and large40×60. ASan/UBSan validate all12groups and held/released image-key changes. Candidateebc57e0e12111ceb builds/audits; actual Standard→Touch→Dual-hand selection/OK/resume works through real200ms touches. Old3capture run has exact3712normal,3712pressed,944Touch-zoom pixels, zero mismatches. Dual-hand small appearance/basicCirclePad observed, no additional dump captured. All3rawVM analyzed/retired. Remaining HUD/fade/other-scene/physical acceptance open.

- [x] Native normal camera HUD is integrated: exact encoded/palette bytes and four-piece geometry identify each group; only proven frontmost groups suppress their16old sprites. New320×240overlay retains transparent map interiors, caches unchanged images and reduces25DSsprite draws to9plus1native draw. Oldcastle21c4eeb0 and NewBob754d93d2 complete3RAM captures each; scene/position/allocator checks pass. New `.lower` synchronized game-target readback avoids dump-progress contamination in legacy `.render`; all3Bob captures match3712opaque native pixels exactly. Pause/upper statistics/resume inspected in Old. Pressed variants/other HUD/physical checks remain pending.

- [x] Native HUD foundation: Python authors14transparent pixel assets (camera arrows, normal/pressed40×60frames, star and multiply), with integer1:1 C++ painter. Offline original-ELF-matched RAM decoding under ASan/UBSan identifies25lower OAM sprites and4camera groups, each split across4DS objects. Normal camera groups are now integrated and pixel-verified below; remaining HUD/pressed variants stay open. Original portraits/map/priority/brightness remain intact. This does not close the all-HUD requirement.

- [x] Instrumented complete candidate43f004df3b3bd639 builds/audits; Old-model castle/Yoshi run completes3RAM captures with actual400×240 geometry and0.8wide projection, distinct sampled positions and real CUA movement/jump. Queue profiling separates synchronized begin7.880ms from unpaced begin0.003ms and explicit-flush end0.322ms. No synchronization removed. Lab lower-panel dirty caching removes per-update repaint/publication; measured composition means18.402/16.227→1.911/1.929ms in two interactive sample pairs, with valid owner/position/allocator reports and responsive mode/character/ability UI. All5new rawVM files analyzed/retired with receipts. Physical FPS and final ten-scenario matrix remain open.

- [x] Two real Old-model lab RAM captures complete with visible DUMPING RAM progress and resumed movement. Exact ELF code (2901952bytes), runtime anchors, active native owner/actor and DS↔solver positions validate in both Mario N64 and Luigi Hybrid. Allocator reciprocal links and linear/VRAM lists pass. New reproducible audit retains reports/trace and retires only these two analyzed .vm files (228673632bytes); user dumps untouched. Not physical crash/FPS or final campaign acceptance.

- [x] Old-model interactive lab verifies Original CirclePad, Hybrid Luigi/Yoshi DS handoffs and N64 Yoshi empty-target action/return. Found and fixed phantom re-jump caused by losing held-button history on native recreation. Actual Controller history seeding preserves new presses;1022session steps and420saved-world regression steps pass. Follow-up Old UI log proves23grounded native updates with B still held, followed by a valid new jump; no fault. Full abilities/progression/scene and physical performance remain pending.

- [x] New separate lab links the native DS-world backend and releases native ownership for DS delegation/scene/control changes; Wario table ratios apply in both native modes. Immutable3DSX75f09493ELF/a2bb48caimage runs in isolated New-model Azahar:960scripted presentation steps,1016logged native ticks, four-character clip/foot/voice API checks and nonzero submitted PCM pass with no adapter fault. Main Controller/Hybrid/DS abilities/other scenes/hardware remain pending; mainc8126d84unchanged.

- [x] Unified native DS backend now executes with the actual host N64 solver and original ARM queries in a fresh saved-RAM copy:420steps(180walk/jump+240directed walls), eight wall positions,612filter calls, no bridge errors; one no-floor spawn is explicitly not executed. Per-tick wall refresh preserves every recorded result while reducing registry checks72336→10320and geometry reads13297→2961. Frozen-world CPU evidence only; main-game/scene/ability integration and real FPS remain pending.

- [x] N64 core carry provider now routes registered DS platform transport with staged position/yaw and no legacy fallback on error; unregistration restores original path. Stable32-handle owner costs2320ARM bytes and preserves pinned/replaced identities. Session gate passes960ticks including120platform steps and rejects missing carry ownership. Unified DsWorldSource callbacks compile on host/ARM(183720resident bytes); saved-world execution is now recorded below; production/hazard/scene integration remains pending. Weekly use21%.

- [x] Added original-normal/plane/material surface descriptor:1243ordinary captured contacts pass,2water contacts excluded; moving handles/effects/unknown friction are explicit, failed outputs unchanged. ARM build and ASan/UBSan pass. Original virtual platform transport matches six controlled transforms exactly,12stale references rejected. Fixed pinning of nested floor/ceiling/wall/object-floor transforms even without a direct active platform; actual-instance ownership and840session ticks pass. Stable platform owner/core carry hook/main activation still pending.

- [x] Native prism enumeration matches1245saved triangles; original DS actor/CLPS filtering passes12289ARM cases including wall-only effect0x14/bit0x40. Added full-registry uncached wall reference and bounded preload cache:451entries,181392ARM bytes, ten cached queries byte-identical to reference, four geometry refresh/enumeration reads versus1245(excludes hit revalidation). Rotated moving floor-to-wall refresh and stale/capacity/fault staging pass. Still needs scene hooks, material/platform ownership and main-game activation.

- [x] Added fractional DS-triangle N64 wall narrow phase.37800actual imported-engine comparisons agree(3774hits), including both projection directions, boundaries/radius cap and a six-wall corner with first-four retention. ASan/UBSan instrument test/original collision TU/surface loader; ARM syntax passes. WorldSession now uses this kernel in its840tick synthetic-world gate. Native candidate enumeration/pass-through/material/platform integration remains pending.

- [x] Added live KCL prism snapshot with registry/owner/material/index validation;1245saved triangles and26stale/malformed rejections pass ARM replay. Fractional world conversion passes1254captured/controlled records under ASan/UBSan plus ARM syntax check, including six transforms and three extra-Y-scale cases. Still unlinked from production movement; real wall/platform/gameplay wiring remains pending.

- [x] Diagnose submitted physical dump crash: incomplete VRAM payload and matching FS abort register. Preserve user's original artifacts.
- [x] Bounded64KiB ordinary-RAM staging before FS writes; include mapped VRAM rather than silently excluding it. Integrity/error tests pass.
- [x] Native lower dump progress card, no per-progress allocations or graphics ownership changes. Real L+R+A and complete VRAM payload observed in Azahar.
- [x] Native320×240 root pause and current Options/Controller/Audio/Display/Developer/Update/confirmation artwork. Original game state and input remain the owner.
- [x] Stop custom pause glyph/frame writes to retail VRAM. Native UI removes only EngineB BG0/BG1/OBJ from the PPU's local view; retain map BG2/BG3 and actual register bytes.
- [x] Two real PPU fixtures: unchanged default output and repeated replacement off/on/off match original masked-reference pixels under ASan/UBSan.
- [x] Real-game pause/Options/Audio/Update returns observed without garbage tiles, original map and upper level/mission/score text preserved (candidate64d51b).
- [x] Larger Audio panel, fitted percent and native slider/sign pixels. Real touch reached100/195/200; default100 and range0..200; fixed-point saturating mixer implementation retained.
- [x] CirclePad analog applies to all three original DS button modes, retaining button bytes and real stylus priority; host tests cover all modes.
- [x] Actual RAM proves resumed Original256×192 versus Native400×240 Wide. Paused geometry remains held until resume. Source now displays target dimensions and an apply-on-resume hint.
- [x] Fields keep a stable body font when values change; root/category titles retain colored game glyphs.
- [x] New Bob/Mario scenario completed3automatic captures, real movement/jump/camera input,3additional explicit resolution captures. All6allocator lists validate, rawVM retired after analysis. Reports and GPU/trace evidence preserved.
- [x] Checkpoint474bc61480b1457a builds/audits/packages; Old castle/Yoshi Buttons inspection is recorded separately from64d51b New Bob UI inspection. Immediate upper-diagram preview refinement builds/audits in7183d9ec65167a5a; its live transition check is pending.
- [x] Optional N64 floor-provider routing builds for host and ARM; the CPU differential closes the earlier one missing hit and five large height differences across951 original-DS ARM floor queries. Unregistered behavior remains the existing lab mesh. This is not production movement acceptance.
- [x] ARM world-ground bridge invokes the original24-collider registry with actor exclusion, original CLPS words and world normal/height/contact identity. All24slots and951differential queries pass; no allocation/I/O in the bridge. Moving-platform geometry and stable contact lifetimes remain open.
- [x] Integer-world N64 floor admission shim includes its78-unit allowance, exact equality boundary and-11000lower limit. Eight flat-floor boundary cases and four overflow/lower-limit rejections pass on ARM; slopes and movers still need directed semantics tests.
- [x] Extended near-contact gate:2067queries over234sloped castle triangles pass; total static bridge gate4015ARM calls.
- [x] Replay saved physical-capture state with byte-exact executable/anchors. The two moving-mesh triangles are water, correctly excluded from ordinary ground. Added a separate water-query bridge; both triangles agree with the direct original query.
- [x] Original Transform replay checks translation, quarter yaw and pitch on both water triangles (six cases), preserving world normals; measured fixed-point ray-height error remains below0.107world units. This is a controlled offline fixture, not physical platform gameplay.
- [x] Added original world segment-query bridge;20real castle ceiling contacts agree with direct queries, and four degenerate/overflow segment inputs leave output unchanged. N64 ceiling admission and wall-cylinder behavior remain pending.
- [x] Added bounded stable surface storage:256slots cost30736bytes on ARM; no allocation/I/O while acquiring contacts, no copying/moving the owner, and every retained/current-tick contact is protected from eviction. ASan/UBSan pass10000churn ticks with50000new contacts and three pinned old contacts. Controller contact enumeration and production integration remain pending.
- [x] N64 core now exports actual retained-contact enumeration for all live instances: floor/ceil/wall/object-floor and platform, without changing bound solver state. Host/ARM builds pass; pins recognize the permanent water pseudo-floor and reject arbitrary foreign geometry. Two real headless instances and360jump/churn ticks pass with modified headless/ownership code sanitizer-instrumented. Main-game wiring and physical acceptance remain pending.
- [x] Long-ray diagnosis found32-bit plane-product overflow and Q12 ratio precision loss in the adapted raycast. Generated-copy fix uses wide products and a high-precision path when overflow/segment length requires it. Twenty100000-unit ceiling rays now match original prism-plane tolerances (maximum0.044305world units), while tested short/water/transform outputs remain identical to the original fixture. Full integrated candidatec8126d8431f00a80 builds/audits with frozen source evidence; emulator/hardware regression remains pending.
- [x] Added DS-world N64 ceiling admission query withY-78boundary,100000sentinel and owner-validated plane refinement. Sixty near-boundary cases across20real castle ceiling faces and six empty/invalid cases pass in ARM saved-capture replay. Generated N64 core ceiling-provider entry builds for host/ARM; actual find_ceil hit/miss/unregister checks prove no fallback to the old mesh. Main DS-world callback/cache wiring remains pending.
- [x] Added exclusive WorldSession ownership of N64 instances/callbacks/cache. It pins contacts before create/tick, resolves source identity/properties into stable records, stages output on failure, and deletes solvers before teardown. Synthetic-world840tick gate covers two actors, wall stop, low ceiling, raw CirclePad adjustment, resolver failure/failed creation and reopen/destruction under the stated sanitizer scope. Real DS wall/CLPS/platform sources and main-game wiring remain pending.
- [x] Added N64 wall-provider entry with staged correction validation: malformed/NaN/vertical movement/invalid contact-count results preserve caller coordinates and do not fall through to a mesh. Host/ARM core builds and sanitizer component gate pass.
- [x] Added ordinary CLPS locomotion profile: six DS friction classes map to N64, preserving class5slide terrain, raw words, sound/parameter/filter fields and class1latch metadata.24582DS-vs-N64 normal/class cases and1850original-ARM getter comparisons pass. All23captured entries decode(22solid/1water); DS effects/unknown classes are explicitly excluded from ordinary mapping and still need gameplay delegation.
- [x] Fixed scaled-Y contact-plane origin in the adapter: SclY's inherited triangle-origin method omits its extra scale, so plane refinement now uses the original collision transform. Three controlled offline cloned-SclY cases(0.5/1/2scale) pass; actual stretched-platform gameplay remains pending.

## Still being implemented or awaiting acceptance

- [ ] Physical regression of the VRAM/FS dump crash on the user's actual Old/New consoles. Emulator captures do not close this.
- [ ] Complete broad lower-HUD acceptance beyond the implemented camera/counter/portrait/map/cap templates, including effects/occlusion and uncommon game states. Keep original guarded fallback and map/castle backgrounds.
- [x] Controller Buttons submenu for DS Standard/Touch/Dual-hand; nested acceptance/cancellation tested, actual Touch movement verified in Old-model Azahar (474bc614).
- [x] Independent Control submenu and real movement physics Original/Hybrid/Nintendo64 implemented and captured in the main Oldcastle executable. Broader DS button-mode/other-scene/physical acceptance remains pending.
- [x] Integrate real Hybrid/N64 movement solver into the main game beyond the isolated lab; one movement owner and original DS ability/interaction handoffs.
- [ ] Complete broad real-game acceptance for characters, progression, stage transitions, moving geometry, transformations, animations/audio and physical consoles.
- [ ] Complete the collision adapter: retain N64 floor admission semantics (including its78-unit allowance), wall/ceiling queries, stable surface storage and moving-platform ownership before registering it in production. World-query routing alone is insufficient.
- [ ] Keep C-Stick sensitivity and CirclePad sensitivity visible; remove user-facing debounce/deadzone/vertical-speed clutter. Verify actual horizontal/vertical camera angular response in the real game, beyond symmetric host math.
- [ ] Full directed Developer/confirmation/Controller visual and touch/keyboard return regression with native pixels; confirm no reappearance of discarded blue options UI.
- [ ] Live Select diagnostics: retain movement/B/camera, close only with Select; complete distribution/information polish and actual input regression on the final binary.
- [ ] Physical initial-star black/gray backdrop regression; previous emulator/GPU evidence was black, user's physical report remains relevant.
- [ ] Sustained4:3 black margins across scene transitions and effects; retain Wide horizontal culling correction and check visible actors at both edges.
- [ ] Validate Native/Wide on real gameplay/effects and defaults. Preserve feedback paths when required; explain actual fallback rather than reversing correct labels.
- [ ] Auditory verification of sound modes and0/100/200 gain on hardware; source/visual tests and muted emulator do not prove sound quality.
- [ ] Final icon/banner acceptance using the user's approved artwork; real HOME Menu visual proof remains separate from BNR/CGFX/CIA validation.
- [ ] Recheck relevant upstream100%-matched decomp changes, importing useful reviewed changes without replacing the pinned adapted dependency wholesale.
- [ ] Final same-binary ten-scenario performance matrix with3captures each, distinct characters/zones and real input. Prior mixed-candidate30capture matrix and the new Bob case do not complete this final matrix.
- [ ] Analyze actual CPU/GPU/I/O scopes, optimize measured redundant work without changing game behavior, retain reports and retire only analyzed new dumps. Do not sum overlapping scopes or equate a VM snapshot with a process profiler.
- [ ] Stable60FPS target on Old and New with Native/Wide: physical acceptance still pending; emulator60Hz presentation or renderer interpolation is not proof of60 game updates or console performance.
- [ ] Final complete CIA/3DSX, private local distribution, short verified link and decoded/hash-checked QR. Existing experimental QR is not the completed release.

## Earlier features to retain during final regression

- [ ] ROM-only setup accepts an arbitrary `.nds` basename under`SD:/3ds/sm64ds/`, with supported-ROM identity checks; arbitrary name does not mean arbitrary game/region.
- [ ] Prepared single-file assets, desktop packer appearance/drag/save flow, six-language console loader and English fallback.
- [ ] Approved centered logo/background; extraction-only long-wait/almost-there text, compact cancel button and single-line translations.
- [ ] Closed-lid extraction continuation and green completion LED on actual hardware, bounded RAM use and interrupted-write handling.
- [ ] Future Update page remains an honest placeholder until GitHub updating is actually implemented and authorized.

Evidence: immutable candidate source/build/audit directories and per-attempt reports under`build/integrated-native/`; detailed chronology in`IN_GAME_OPTIONS_PROGRESS.md` and`PERFORMANCE_CAMPAIGN.md`. ROMs, saves, prepared copyrighted data and raw process captures are private and never part of a public package.

## NP49 renderer adaptation — 2026-10-19

- [x] Study MKW-3DS native rendering mechanisms and inventory 13 direct forks;
  distinguish Epic MK64 from the Wii codebase (see `NP49_RENDERER_STUDY.md`).
- [x] Add exact-value mesh uniform reuse and invalidate material state after
  highlight drawing in the generated integration; preserve pinned vendor.
- [x] Pass host equivalence/sanitizer gates, full build and package verification.
- [x] Verify candidate eb76eabaf6f4e95c includes all 47 current options source files.
- [x] Close Old castle-Yoshi and New Bob-Mario Azahar diagnostic sessions with
  three validated captures each and no detected emulator fault.
- [ ] Measure same-scene physical Old/New baseline-versus-candidate frame times.
- [ ] Complete broader UI, gameplay, stereo, sound and lifecycle acceptance.

Uniform work avoided in final cumulative emulator snapshots: Old 22.49%, New
20.34%. These percentages are not FPS improvements. Current result and limits
are documented in `NP49_RENDERER_STUDY.md`; this is a local candidate only.
