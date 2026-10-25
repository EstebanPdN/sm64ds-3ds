# In-game options and presentation fixes

Updated 2026-10-16. Source/host/ARM checks are separate from emulator and physical-device acceptance.

## Implemented, integrated acceptance pending

- Actual Stage pause menu extension, Audio category, consistent original 8px submenu font, Dump RAM label.
- Original 4:3 gameplay margins no longer use the title background extension.
- Native mono target allocation avoids unused right-eye high-resolution targets; releases retained menu filter surfaces before allocation.
- Wide actor frustum matches the horizontal 5/4 expansion, without changing distance or vertical planes.
- Select-only live telemetry panel; no independent options menu, B and movement stay available to the game.
- Pixel-authored diagnostic frame generator and two-column performance/memory layout; taller lower status card.
- Game-font FPS strip anchored to the upper-right LCD margin.
- Saved audio.volume 0..200, default100; saturating PCM16 gain, bit-exact at100. Native menu slider integrated; enlarged panel visually checked.
- Pause revision in progress: retain original upper HUD (level/stars/etc), replace only its background with frozen gameplay; preserve original bottom map. Earlier whole-upper freeze removed HUD and was rejected.

## Validation still required

- Full integrated build and directed acquisition completed for f3c6; f8cb adds title-veil and pause-priority corrections and passes build/audit.
- Native actual viewport/panel dimensions and Wide visible actors at both edges.
- Native pause frozen-level composition, including native/stereo transitions and map OBJ checks.
- Select panel while moving/jumping/camera, close with Select, FPS upper-right.
- Volume sound modes, mute/100/200, touch/button slider, saved settings.
- Initial star gray backdrop: producer identified and fixed. Same-frame RAM plus all GPU stages in f8cb confirm opaque black, while retaining star/text. See title-gray-fix-validation.json in attempt1792139615056111000.
- Physical Old/New3DS behavior and sustained60 FPS remain unverified.

## Continuation 2026-10-16: same-frame title diagnosis

- Complete capture epoch900, scene1, candidate f3c6eb422328ba64: 120,631,296 readable payload bytes, complete coverage to0x40000000. VM SHA256660392235ee043131edc862817cc3715cd4f31108551aa2117ebed7e45657a57.
- Geometry, composed panel and LCD readback all contain RGB16 background. The GX frame has an opaque black fullscreen plate followed by an untextured white fullscreen quad, polygon22, alpha1. Its DS blend weight2/32 produces the observed gray.
- Live object is menu root+0x110, initialized by func_ov007_020b2998: f_108[2], color0x7fff, f_50=0x1000. Prepared-source change makes only this permanent veil alpha0. No global color replacement, alpha policy or fade change. Rebuilt target f8cb confirms black at every GPU stage; fixed veil opacity0 in RAM.
- Live normal-input game entry and pause verified: bottom map remains, level text and stars remain over frozen level. Original cream stats frame is still occluded and requires a layer-priority correction. Do not mark top-screen preservation fully accepted.
- Audio larger panel verified visually in Azahar: label and slider have margins and fit inside192x56 panel. Options categories share original font/button format.
- Campaign remains0/10 completed,0/30 captures. Title capture is diagnostic evidence only.

- Pause RAM confirms BG2 priority1 (cream frame) and text priority0. New held geometry priority2 preserves those layers; target pause visual confirmation still pending because Mac locked.
- First automatic original-API scenario, Old castle/Yoshi, is running; no completed campaign result claimed yet.

## Latest continuation

- Candidate93ab94f8365c858b is fully built/audited/packaged. The normal runtime retains the fixed title veil, priority2 held pause geometry, enlarged Audio panel and default-disabled shadow probes. All benchmark actions require the strict private marker and Developer diagnostics; normal installs run none of them.
- Original SaveData::SetCharacterIntro(7) is used only as a declared isolated benchmark prerequisite to avoid running the castle opening script inside other courses. The actual original implementation passed ASan/UBSan: only bit7 changes, character unlock bits/other bytes remain unchanged, no cart writer linked.
- Bob Old is waiting for legitimate message352/phase7 (confirmed from full RAM), and is not counted as a successful performance scenario. Manual UI checks await macOS unlock.
- Accepted spawn-baseline results and actual capture counts are tracked in PERFORMANCE_CAMPAIGN.md and its immutable per-attempt reports. None establishes movement, leak freedom or physical Old/New FPS.

- Automatic subset finished:6/10 spawn baselines,18/30 complete captures. Each has validated400x240 Native+Wide, exact scene/character/model and allocator/timing analysis. No active acquisition remains; remaining interactive tests require Mac unlock. Hardware FPS and full leak-freedom remain unverified.

## 2026-10-17 continuation: real input and measured draw setup

The Mac is accessible again. Historical lock notes above describe previous attempts, not the current blocker. The active goal now targets stable60 FPS on **Old and New**, superseding Old>=30. Physical acceptance remains pending.

- [x] Real CUA input through original title, Adventure, File A, the complete opening and original dialogue; no process RAM writes.
- [x] Initial star background observed black on both screens, consistent with earlier RAM/GPU validation.
- [x] Exterior Yoshi walking, C-stick I/J/K/L input and B jump observed. Exact four-direction angular comparison remains a separate pending gate.
- [x] Original Continue/Controller/Options/Quit frames remain intact after repeated Options/Developer/Audio returns in this run; the prior garbage tiles did not recur in those returns. Controller return still needs its own check.
- [x] Current-level frozen pause geometry and original bottom map observed, including level/stars/cream statistics frame in several frames. Intermittent apparent absence of the upper statistics panel is **not closed**: direct upper GPU/LCD captures were added to distinguish actual compositor output from UI-capture artifacts.
- [x] Audio panel margins and touch selection0/100/200 verified visually; emulator muted, so auditory acceptance is not claimed.
- [x] Update text legible inside its cream panel.
- [x] Select overlay accepts movement and B jump and closes with Select back to the map. Colored percentage-number font at upper-right FPS verified visually.
- [x] Normal-play attempt1792292843159319000 closed without emulator CPU/memory fault. Three paused captures after movement contain three distinct exterior positions; these are explicitly mixed menu/gameplay traces, not steady movement FPS percentiles. Their allocator lists validate. The concurrent title capture has inconsistent allocator links and is rejected for allocation totals; it is not evidence of a runtime heap corruption.
- [x] Full readable process VM plus trace/render output now has a separate580,624-byte `.upper` companion (composed256x192 panel and actual400x240 LCD), bounded allocations and failure-path frees checked with ASan/UBSan. It is included in Delete Dumps.
- [x] V2 trace reserved word now optionally carries a signed context marker and pause/control/cutscene flags. Older zero-reserved rows remain unknown. Analysis excludes state boundaries instead of labeling paused/dialogue work as active gameplay.
- [x] Strict private interactive benchmark marker allows300s for legitimate dialogue input; ordinary unattended watchdog remains30s. No automatic dialogue dismissal or input synthesis.
- [x] Benchmark model acceptance now checks the actual `Render()` body selector (`mBodyModelId`) rather than only a loaded target resource or HUD identity. Paused states suspend measurement.
- [x] Whomp/New/Luigi attempt1792294201726986000: original dialogue advanced through real input, actual Luigi body validated,3 complete Native+Wide captures,2 distinct positions,1,684 measured trace rows. No instrumented SD reads/opens in those windows. This is a lower-perimeter route, not whole-level acceptance.
- [x] RAM identified98 adjacent repeated material configurations among368 eligible setups across those captured batches. Original material block replay at draw boundaries passed12 native/non-native/filter/shadow combinations plus24,000+ stress records under ASan/UBSan; setup count368→270. This proves register-state equivalence and reduced setup calls, not a measured FPS gain or raster pixel equivalence.
- [x] Candidate45f9602eb15d9fa2 builds/audits/packages with this material cache. Cache lifetime is one draw scope; shadows, experimental FramePlans and missing textures invalidate it. Draws, matrix binding, order, depth and stencil are retained.
- [ ] Finish direct GPU/LCD diagnosis of upper pause statistics visibility.
- [ ] Directed Controller return, Native/Original switching, black4:3 margins and Wide edge actors.
- [ ] Quantitative live C-stick four-axis check with collision limits distinguished from angular sensitivity.
- [ ] Finish the ten scenario/model matrix and extend standing baselines with movement/camera routes; validate performance changes against relevant prior evidence.
- [ ] Final complete candidate package and verified served CIA/decoded QR after remaining runtime fixes/checks.
- [ ] Physical Old/New sustained FPS, audio/save/scene transitions, lid/notification and HOME banner acceptance. No emulator measurement establishes these.

Raw retention:18 analyzed baseline VM files retired with hashes and per-attempt receipts, freeing2,171,344,464bytes. Reports/traces/anchors/renders and replay fixtures remain. New series are analyzed/classified before retirement; rejected/non-atomic observations are recorded explicitly.

### Further runtime evidence,2026-10-17

- [x] Historical ten-pair matrix now has30 validated capture samples; see PERFORMANCE_CAMPAIGN.md for exact mixed-candidate and movement limits. This does not close final-candidate or physical acceptance.
- [x] Controller back transition observed with intact original main button frames on New/Bob45f.
- [x] Original256x192 geometry/centered320x240 output and restored Native400x240/Wide verified from Old/Bob45f RAM, independently of option labels.
- [x] Upper pause course/mission/score,stars/coins and bottom map retained on Bob; synchronized v2 GPU target confirms the Whomp upper pause content too.
- [x] Replaced diagnostic-only upper CPU-buffer read with synchronized final-target readback. V2 header distinguishes this source; allocation384,000bytes is freed after capture, and readback failure propagates. Host ASan/UBSan and full build/audit/package pass on d1d38fa9ae9df88b.
- [x] V2 Whomp/Original4:3 captured side margins are RGB black below the FPS overlay. This is one observed GPU-target frame during fade, not an all-transitions or physical LCD certificate.
- [ ] d1d New/Whomp exited unexpectedly after three complete captures and directed display checks. No emulated CPU fault or macOS crash report found; source cause unresolved. Its three files are analyzed but the run is explicitly excluded from final regression acceptance. Supervisor now records pre-cleanup process return codes for future diagnosis.
- [ ] Continue quantitative four-axis camera/culling tests and final-candidate movement/performance regressions. Original six standing baselines remain standing evidence.
- [ ] Final QR remains pending runtime acceptance; compiled CIA is not described as a completed60FPS port.

All analyzed normal-flow/Whomp/Bob raw VM series above have per-attempt retirement receipts; reports, direct target PNGs, traces and reproducible material-register C++ replay fixtures remain. The new diagnostic change does not establish an additional renderer performance improvement.

Follow-up d1d/Old Whomp attempt1792299672727231000 closed under the interactive dialogue watchdog: five A presses did not complete the original tutorial before300s. Logical target character1 still had body selector3; this is deliberately rejected as playable Luigi. Zero benchmark samples; one complete diagnostic was hash/anchor/trace/allocator classified and retired. This is an input-delivery limitation of that attempt, not a new render-performance result. No emulator acquisition remains active after this closure.

### Periodic SD telemetry,2026-10-17

- [x] Found unbuffered SD writes for periodic render60,OBJ,GPU,FPS and VBlank statistics in normal play. A prior Bob log contained1,620 matching lines(187,090bytes). Input/scene/error/startup events are separate and retained.
- [x] Gate periodic telemetry with the existing Developer FullRAM setting. On-screen FPS uses its own counters and remains active. Disabled JobMetrics windows reset before sorting/formatting so re-enabling cannot report stale timing intervals.
- [x] ASan/UBSan test exercises1,000 completed jobs with telemetry disabled: zero stdout bytes; enabled mode still emits a fresh complete timing report. Full build/audit/package passes for3f26264004971f83.
- [x] New/Bob attempt1792301360668801000 closed as planned, no CPU fault,3 complete captures and3,475 measured rows. Real movement/jump and four200ms C-stick direction inputs observed; no numerical four-axis equality claim.
- [x] FullRAM Off persisted. A115.18second live window appended zero periodic statistics and only192bytes of input events. Game remained visible with upper-right colored FPS. The UI sequence exited the course into castle interior; therefore it is not a same-scene timing comparison. Raw3VM analyzed/retired, reports retained.
- [x] Experimental CIA and LAN QR prepared and HTTP/hash/QR decoded URL verified. Receipt:`build/integrated-native/delivery-3f26264004971f83.json`; CIA SHA25661e1dbfbbf1e813ca4dcab290c414d29651899812e9b3309023ba9e4fe391bdc. Served directory contains only CIA and public README. No GitHub publication or physical acceptance claimed.
- [ ] Physical Old/New results, final camera/culling/runtime regression and stable60FPS target remain open. The experimental QR does not mark the goal complete.

## 2026-10-18: physical dump failure and native UI foundation

- [x] Matched the submitted incomplete VM executable text against candidate3f26264004971f83. The final readable mapping was VRAM0x1f000000..0x1f600000; file contained144 bytes of its declared6MiB payload. Luma's ARM11 `fs` abort contains r6=0x1f000090, exactly the next byte. This identifies the unsafe direct mapped-VRAM-to-FS transfer, rather than a generic heap exhaustion claim. Private evidence: `build/integrated-native/physical-dump-crash-1792318619198877000.json`.
- [x] Added a fixed64KiB aligned process-RAM staging buffer; capture uses word loads from mapped memory and passes only the staging address to fwrite. VRAM remains included. ASan/UBSan transfer regression covers complete6MiB integrity, all96 chunks, write failure and invalid alignment.
- [x] Added lower-screen dump progress before capture, during region writes and after renderer readback. No per-update heap allocation, upper-screen swap, graphics initialization/teardown or VBlank wait. Current percentage follows scanned address-space coverage, with final10% reserved for finalization/readback; it is not a time estimate.
- [x] Authored native320×240 UI frame/sprite foundation using Python pixel writes. Bitmap fonts remain1:1. Dump screen uses the new painter. Guarded host rendering verifies the card and text fit.
- [x] Candidate71f532baf1c21ec0 built and audited with all integrated host gates; emulator/physical dump regression still pending at this entry.
- [ ] The staging fix still needs real-console retesting. Emulator success cannot reproduce or close a hardware FS IPC fault.
- [ ] Complete native lower-menu/HUD integration remains pending; generated sprites alone do not resolve the original game's corrupt menu tiles.
- [ ] Controller must contain Buttons (DS button modes) and Control (Original/Hybrid/Nintendo64 physics), plus C-Stick/CirclePad sensitivity. All physics modes use CirclePad. Isolated movement-lab behavior is not production integration.
- [ ] Resolution appearance report remains open: test actual target sizes and pause-delayed layout application; don't simply reverse already-correct enums.
- [ ] Final scenario regression, physical Old/New performance, final package/QR and remaining whole-list acceptance remain open.

### Dump regression receipt, candidate71f532baf1c21ec0

- [x] Independent New-model Azahar attempt1792319365607287000 closed at182.33s by its time bound, with no emulator CPU/memory fault. One automatic title and one real L+R+A menu capture completed; each contains the entire6MiB VRAM mapping and120,631,296 readable payload bytes. Both rawVM files were analyzed and retired (241,264,592 total disk bytes); immutable acquisition, hashes, traces, GPU images and reports remain. User-supplied physical crash artifacts were preserved.
- [x] CUA showed the new native lower progress card with a partially filled bar, while the upper Mario title remained visible. The original menu resumed after the automatic capture.
- [x] Manual menu allocator lists validate. The concurrent initial-title capture has inconsistent reciprocal allocator links and is rejected for allocation totals; that is not treated as proof of runtime corruption. Neither capture is a gameplay performance measurement.
- [ ] Hardware VRAM/FS crash regression is still required. This candidate is a dump-fix checkpoint, not the completed UI/physics release.

## Native lower pause UI replacement, 2026-10-18

- [x] Root Continue/Controller/Options/Quit and all current option pages draw to a native320×240 transparent image. Pixel-authored frames and bitmap glyphs are sampled1:1; no fractional DS UI scaling. Map/castle backgrounds remain original game layers.
- [x] Removed custom submenu glyph/panel writes and the root CONTROLLER glyph overwrite from runtime VRAM. Original state transitions/input stay active; the original framework is restored before returning to its parent.
- [x] The PPU's local EngineB DISPCNT view hides only BG0/BG1/OBJ while a native pause image and its GPU texture exist. Actual registers and banks remain unchanged; BG2/BG3 map remains. Native image blends alpha over that map. Existing diagnostics still use the same bounded image channel.
- [x] Differential PPU gate covers two retained real RAM fixtures: existing64-case pixel output is identical when disabled, and24 off/on/off cycles for both engines match original PPU output with explicitly masked private fixture registers. Real register words remain unchanged in the production variant. ASan/UBSan pass. The original alpha/affine equivalence gates remain required separately.
- [x] Native audio panel has76px height,280px width, fitted label/percentage, authored +/- punctuation, and slider range0..200. Native touch rectangles follow the new panel, footer and slider.
- [x] All three DS button modes receive the same CirclePad analog movement. Host test preserves held/pressed buttons and a real stylus sample; physics still remains DS until the separate movement integration.
- [x] Candidate64d51b670e944a00 built/audited and locally packaged; CIA SHA256556081bba5f50cbc0b2ec4a230aa2f5b0059018fe2eb3c64bbdd5a9903315dff. Directed New-model Bob attempt1792320348377193000 is the current runtime inspection.
- [x] CUA observed pause root, Options, Audio and Update with the map intact and original upper level/mission/score counters present. Repeated Audio/Update returns preserved the root buttons. Volume100/195/200 was observed through real touch input; auditory acceptance is separate (isolated emulator muted).
- [ ] Remaining bottom gameplay HUD sprites still use their DS assets; replacing pause UI alone does not complete the user's all-sprites request.
- [ ] Buttons/Control hierarchy, actual N64/Hybrid production physics, resolution transition diagnosis, full scenario regression and physical acceptance remain open.

- Resolution diagnosis from actual RAM: selecting Original while paused retains the held400×240 Native geometry. After accepting and resuming, Original renders256×192 with the same400×240 Wide output. This reproduces the misleading preview rather than an enum inversion. Native restoration is being checked; UI needs an explicit apply-on-resume note.

- New Bob attempt1792320348377193000 closed as planned after three campaign captures plus three explicit post-campaign resolution captures. All six allocator snapshots validate; all VM payloads were analyzed and retired with hashes (723,793,776 disk bytes). Campaign has3,254 selected steady rows and3 distinct snapshot positions. It remains one mixed movement/menu scenario, not whole-game or hardware acceptance.
- Resumed Original256×192 and Native400×240 were verified from actual g_display in separate captures; selecting Original during pause retained400×240 geometry, proving delayed application. Source now adds an apply-on-resume hint and explicit target dimensions, and keeps field text style stable when switching values. These last source refinements still need the next integrated build.

- Candidate3d9352b8b23d8f33 now builds/audits with the stable field font and explicit resolution/apply-on-resume note. Directed runtime inspection above belongs to64d51b670e944a00; do not silently attribute it to the newer binary. Native UI PPU oracle remains pixel-equivalent. No new scenario execution is needed solely to repeat the unchanged PPU pipeline before the remaining controller work is integrated.
- Timing scopes in the Bob run overlap and must not be summed as frame time. WorkerMesh mean13.57ms includes WorkerCore mean12.52ms; WorkerFrameBegin includes waits (mean8.31ms). GameFsRead/Open are absent from nonzero scopes; GameFsLoad mean0.000845ms/p95zero/max0.443ms. This does not identify ongoing SD traffic as the active bottleneck and does not justify unbounded extra preloading. All values are from Azahar, not physical CPU timings.

## Controller/Buttons and original collider evidence, 2026-10-18

- [x] Native Controller row0 opens a separate Buttons page with Standard/Touch/Dual-hand. Inner OK returns to Controller; only its outer OK commits the session, and cancellation restores the original settings. Keyboard/touch/radio state and sensitivity independence pass host sanitizer tests.
- [x] Candidate474bc61480b1457a built/audited/packaged. Old-model castle/Yoshi attempt1792321678756219000 closed as planned:3stationary campaign samples plus1manual Touch diagnostic. All4rawVM were analyzed and retired (482,512,800bytes), reports retained.
- [x] Real UI showed all3Buttons options; Touch was selected and applied, CirclePadUp was held1200ms then1000ms. RAM shows command record mode1,66analog injections and worldZ displacement606,208Fix12 units from baseline. This is actual Touch movement evidence, not just an injected command-record test. Reopening Controller shows the original Touch upper diagram. Dual-hand real-game movement remains pending.
- [x] A multi-nonmodifier CUA chord was rejected, so no held-input capture from that attempted chord is claimed; the successful diagnostic used ordinary Shift+A after releasing movement.
- [x] Found immediate diagram preview lag: original Controller changes the upper BG2screen-map selection. Source now uses the same original mapping (0xd/0xe/0xf), preserving flags and leaving glyph banks untouched. Exhaustive flag/mode host checks pass; integrated build/runtime preview check remains pending.
- [x] New bounded saved-capture reader explicitly rejects missing bytes in partial VM files. Four disk-format tests cover complete reads, crossing regions, truncated payload/header and falsely complete headers.
- [x] Original physical crash prefix was read without modifying it; executable .text matches the original3f262640 candidate byte-for-byte. Original registry has1243static KCL triangles plus2moving triangles, with valid CLPS indices. Prefix remains incomplete and is not accepted as whole RAM/allocator/performance proof.
- [x] Added a no-allocation KCL prism decoder that preserves sub-unit coordinates and original attribute indices. It passes plane, winding, normal and attribute checks for all1245captured prisms under ASan/UBSan; ARM syntax check passes. The reproducible private fixture stays under build/movement-adapter. This is an adapter component, not a runtime physics integration.
- [ ] Moving-collider world transforms, surface-property semantics, stage/lifetime ownership and core movement handoff still need integration and behavioral acceptance. Hybrid/N64 stay unavailable in production until real behavior exists; no cosmetic physics selector was added.

- Candidate7183d9ec65167a5a now builds/audits the immediate original-BG2 controller diagram preview. Its image SHA256 is02357b21a82ad244a1366cf8ce8014107ffa366f850c1f62e1643ef2c12d126a. The actual Buttons/Touch inspection above remains474bc614; no implicit runtime acceptance of the newer binary.

## Production movement collision bridge, 2026-10-18

- [x] Optional floor-provider route compiled into the generated host/ARM N64 core. CPU differential951queries eliminates the earlier one missing floor and five large height discrepancies by calling the original recovered DS ARM query; registered misses never fall through to an approximate mesh.
- [x] Added ARM-only `ds_world_query` bridge: original24-collider registry/virtual dispatch, own-actor exclusion, world normal/height, raw CLPS and contact identity. Independent ARM overlay gate exercises all24slots and951direct-vs-world floor queries, with no mismatches.
- [x] Added explicit N64 integer-coordinate admission shim with78-unit floor allowance and-11000lower limit. Eight near-contact boundary tests plus four range/lower-limit rejections pass; missed/invalid calls preserve output. Latest full gate1948calls, report at`build/movement-adapter/world-ground-gate-latest.json`.
- [ ] Complete slope/mover admission, walls/ceilings, stable N64 surface lifetimes and production movement handoff. These bridges are not registered in the main controller yet; no cosmetic Control-mode labels or new final CIA/QR are claimed.
- [x] Weekly account usage rechecked at17%; limit remains50%. Goal remains active.

- [x] Slope admission expanded to2067near-contact queries over234triangles; static bridge gate now4015ARM calls. No mismatches.
- [x] Original physical-capture replay verifies executable.text and anchors, uses only wholly saved mappings and leaves the original dump untouched. Its moving collider is water; separate ordinary/water bridge entry points preserve that distinction.
- [x] Both water triangles and six controlled offline transform cases pass direct-vs-registry checks. New original world line bridge matches20castle ceiling contacts; four degenerate/overflow segments reject without writing output. Replay gate70calls; it is not physical gameplay or FPS evidence.
- [ ] Next: N64 ceiling admission, wall-cylinder semantics, contact storage/lifetime and solid moving-platform ownership, then production movement integration and the final scenario matrix. No new complete-release QR is claimed.

- [x] Stable contact-storage component added: non-movable fixed-capacity records protect explicitly retained and current-tick contacts, distinguish owner generations and reject foreign references. ASan/UBSan pass10000churn ticks; ARM256-slot size is30736bytes. No per-query allocation or SD access. Receipt:`build/movement-adapter/stable-surfaces-gate-latest.json`.
- [ ] Main-controller enumeration/pinning and lifecycle integration remain required. DS sphere collision is not treated as an equivalent N64 wall solver; full-range ceiling queries also need precision/admission work. Goal stays active; weekly account use remains17% at the latest check.

- [x] Generated N64 core now exports typed per-instance/all-instance retained-contact reads (floor, ceil, wall, object oFloor and platform), preserving the bound global state. Explicit water pseudo-floor ownership and bounded deduplicated pins are implemented. Host/ARM builds and symbol checks pass.
- [x] Two actual headless instances pass ownership/read/deletion tests. A360tick stationary/jump flat-floor fixture survives generation changes and cache churn, reachingY496with no provider fault. Modified headless and ownership code run under ASan/UBSan; other imported core members are not fully sanitizer-instrumented. Report:`build/movement-adapter/solver-contacts-gate-latest.json`.
- [ ] Main port movement wiring, real wall/ceiling/platform handling, character abilities/mode transitions and final scenario/hardware acceptance remain open. Latest weekly account reading18%; goal remains active.

- [x] Long ceiling rays revealed plane-product overflow plus Q12 ratio loss in the adapted raycast (20of20baseline height differences exceed1world unit; largest23337.65625). Fixed in the generated source copy, with a wide/high-precision path and retained short-segment arithmetic.
- [x] Offline ARM corrected replay passes20long prism-plane checks (maximum0.044305world-unit error); saved short ceiling/water/transform outputs equal the original baseline. Pure arithmetic ASan/UBSan/ARM compilation gate is mandatory for integrated builds.
- [x] New full candidatec8126d8431f00a80 built/audited with246frozen source-evidence files. Image SHA256c7e9f0afd6bfad1e6114074ef50188b9dc1b1515de29c1e7b60d83ba50034f9e. No emulator/hardware acceptance or final CIA/QR yet. Weekly account use remains18% at the latest check.

- [x] Added owner-validated world plane refinement and N64 ceiling-admission backend(Y-78,100000sentinel). ARM replay passes60near-boundary ceiling cases plus six empty/invalid cases; latest replay156calls. Existing short/water/transform and long-ray checks remain passing.
- [x] Added optional ceiling provider to generated N64 find_ceil; host/ARM builds pass. Actual core hit/miss/unregister tests verify no accidental fallback to an existing mesh ceiling. The360tick contact/cache test remains passing under the stated instrumentation scope.
- [ ] Connect DS-world ceiling callback to stable surface storage and the main actor alongside walls/platform ownership and CLPS behavior. These adapter changes are not yet a new main-game candidate; c8126d8431f00a80 remains the latest full integrated build. Goal active; latest weekly usage18%.

- [x] WorldSession now owns headless actor lifetime, unified collision callbacks, stable contact resolution/pinning, staged output and teardown. All three geometry callbacks and explicit material/identity resolution are required; real DS wall/material/platform sources still need implementation.
- [x] Extended gate passes840actual solver ticks in a synthetic floor/wall/ceiling world, including120raw-CirclePad ticks, two actors, a wall stop, roof-limited jumping, resolver-error output protection, failed creation and safe reopen/destruction. Modified headless/ownership and C wall-validator tests use ASan/UBSan; imported core coverage remains explicitly limited.
- [x] Optional wall provider is integrated into generated N64 find_wall_collisions; malformed results preserve input coordinates and never fall back to unrelated geometry. Current host/ARM builds and ARM session compilation pass.
- [ ] Main DS-world geometry/material/platform integration and all final game/hardware/scenario gates remain open. No new full-game build/QR follows from this synthetic-world session gate. Latest weekly usage19%; goal active.

- [x] CLPS locomotion profile now preserves six friction classes including class5slide terrain and all raw/sound/parameter/filter metadata. Actual DS-vs-N64 slope comparison passes24582cases; original ARM accessors match1850field comparisons. The23captured material entries classify as22solid/1water.
- [ ] Nonzero DS effects, toxic/water gameplay and unknown friction classes are explicit separate dispositions; hazard/warp/progression ownership still needs the actual gameplay bridge. No silent ordinary-ground mapping is claimed for these surfaces.
- [x] Fixed the adapter's SclY contact-plane origin using the same original extra-Y-scale transform as collision queries. Three controlled offline clone scales pass; saved-capture replay now162calls. This is not live stretched-platform gameplay or a new main-game candidate.
- [ ] Main integration, real DS wall/geometry sources, platform ownership and all final acceptance remain open. Latest weekly account reading20%; goal active.
