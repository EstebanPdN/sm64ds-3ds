# Performance and RAM campaign

Requested 2026-10-16, target revised 2026-10-17. Definitive matrix: **10 runs, 3 complete process-memory captures per run (30 total)**, plus directed movement/menu/camera acceptance. Every run uses Wide + Native. Five scenarios are paired across Azahar Old/New profiles. Emulator timings identify work and regressions; they do not certify physical console FPS. Current hardware objective: **stable60 FPS on both Old and New 3DS**. This replaces the earlier Old >=30 target and remains unverified.

| Run | Model | Scenario | Character | Status | Captures |
| --- | --- | --- | --- | --- | --- |
| 01 | Old | Castle grounds | Yoshi | spawn baseline validated; movement pending | 3/3 |
| 02 | New | Castle grounds | Yoshi | spawn baseline validated; movement pending | 3/3 |
| 03 | Old | Bob-omb Battlefield | Mario | local input observed; three snapshots at same position | 3/3 |
| 04 | New | Bob-omb Battlefield | Mario | local grass/cannon route; two sampled positions | 3/3 |
| 05 | Old | Whomp's Fortress | Luigi | local wall/grass route; two sampled positions | 3/3 |
| 06 | New | Whomp's Fortress | Luigi | lower-perimeter route; two sampled positions | 3/3 |
| 07 | Old | Jolly Roger Bay | Wario | spawn baseline validated; movement pending | 3/3 |
| 08 | New | Jolly Roger Bay | Wario | spawn baseline validated; movement pending | 3/3 |
| 09 | Old | Tick Tock Clock | Wario | spawn baseline validated; movement pending | 3/3 |
| 10 | New | Tick Tock Clock | Wario | spawn baseline validated; movement pending | 3/3 |

Scenario names are the intended matrix; original level/entrance and character APIs must be verified before activation. No fabricated unlocked save, raw RAM edits, rendered scene mockups or repeated unchanged title runs count as a completed gameplay scenario. Diagnostic entry through verified original game APIs must be recorded explicitly if used.

Each run records exact ELF/CIA, settings, ROM/prepared-container hashes, scenario/entrance/character, emulator model, observed state and actual geometry dimensions. Collect three snapshots at separated stable/movement/camera moments; preserve frame/scene/epoch, a bounded timing trace and captured-region completeness. Exclude dump I/O and GPU readback pauses from steady timing windows.

Analysis: frame-time distribution and stalls; CPU transformation/culling/composition; GPU submission/wait; SD open/read/load counters; resident/cache memory, allocation churn and lifetime; compare the same scenario/model after a measured fix. Precache only bounded, reusable data compatible with Old RAM; do not retain all assets indiscriminately.

The earlier title diagnostic (120,632,296B,39regions, complete) is root-cause evidence and **does not count** toward these30 campaign captures.

## Diagnostic attempts, not completed campaign runs

- f8cb / Old castle-Yoshi / attempt1792139704264196000: original Stage3, level1, character3 and body model confirmed. The benchmark observer incorrectly coupled body-model presence to the control predicate; a subsequent original control interruption at update3775 produced a false missing-model abort. Zero campaign captures; excluded from the ten-run count.
- Observer correction: model and control independent; cutscene/dialogue interruptions suspend the clock and require120 stable updates before resuming, preserving prior successful captures. Genuine model/stage failures produce an uncounted full diagnostic capture before stopping. Host ASan/UBSan regression checks include interruptions after the first capture and prove excluded time does not trigger the next sample.
- Next automatic baseline uses600seconds as a safety bound, ending as soon as all3 captures close successfully. Manual movement/camera acceptance remains pending while macOS is locked. Standing original-API baselines must be labelled as such.
- Removed default upstream shadow-target probes: they scheduled up to12 synchronous full-target readbacks/temporary allocations/scans, every600 native frames, and altered the shadow path on probe frames. They now require SM64DS_SHADOW_TARGET_PROBE=1. Explicit Developer RAM/GPU captures remain available. No numeric hardware FPS benefit claimed.

## Accepted baseline evidence (current subset)

Frozen candidate93ab94f8365c858b. Supervisor manifest: build/integrated-native/campaign-1792140797163839000/manifest.json. Each accepted entry has3 complete matching-scene/model captures, real400x240 geometry with projectionX0.8, verified file hashes, original live player/model confirmation and timing windows excluding capture stalls. These are standing spawn baselines; player positions are explicitly retained in the reports and movement/camera checks are not claimed.

RAM allocator analysis reads the captured newlib free bins and libctru linear/VRAM free lists, checking ARM32 instruction inputs and reciprocal links. On castle Old, allocated malloc chunks are38,535,776 /38,526,376 /38,526,488 bytes at the three moments. Linear free space drops exactly524,288 bytes after the first GPU capture, then remains14,035,456 bytes; this matches the renderer's single lazy readback scratch allocation. VRAM free space stays778,240 bytes. This is bounded diagnostic overhead, not a proof that the complete game is leak-free. Malloc committed-arena free bytes must not be confused with total uncommitted heap capacity or console RAM.

The measured castle idle windows contain zero instrumented FS reads or opens; cached Load calls still appear. Do not enlarge precaching based on a supposed SD-read loop that is absent in this baseline. Other zones, transitions and movement require their own evidence.

Bob Old diagnostic on93ab: full process capture confirms message352, phase7, controlDisabled1, actual400x240/projection0.8. This is a legitimate waiting dialogue; the watchdog captures it once and stops without synthesizing input. It contributes0 campaign captures.

Current subset closed:6 paired-model spawn baselines,18/30 complete captures, all analyzed for timing/actual display and allocator state. Remaining4 planned baselines and manual movement/camera/menu acceptance are pending. Aggregate receipt: build/integrated-native/campaign-1792140797163839000/subset-summary.json. No active emulator is left by this supervisor.

## Retention policy revised 2026-10-17

The user explicitly requested deleting each RAM series after analysis. The18 analyzed baseline `.vm` files were revalidated against both analysis reports and their original closed acquisition hashes, then removed with `tools/retire_analyzed_campaign_dumps.py --apply`. Each of the six attempts retains `retired-VM-receipt.json`, the immutable original result, allocator/timing reports, traces, anchors and render outputs. Total raw VM bytes retired:2,171,344,464. Historical capture counts remain valid, but these raw VMs are no longer available for replay. This deletion excludes the original PPU replay fixtures, private inputs, saves and unrelated diagnostics. Future reuse must inspect the retirement receipt rather than claim the raw files still exist.

## 2026-10-17 interactive continuation

Historical matrix now contains30 accepted captures across ten scenario/model pairs. Aggregate receipt: `build/integrated-native/matrix-historical-30-1792298522745888000.json`. This combines candidate93ab spawn baselines, dafd New Whomp, and45f Old Whomp/Old+New Bob. It is **not** a final-binary regression or sustained physical FPS result. Six routes still need movement acceptance; Old Bob movement happened before/between measurements but all three captured positions coincide.

Whomp Old45f:3,251 measured rows; WorkerMesh mean13.465ms/p9518.023ms, WorkerDraw mean3.966ms/p956.265ms. Scopes overlap and cannot be added. Comparing this directly to New/dafd would confound model, route and candidate; no numerical optimization gain is inferred. No instrumented SD reads/opens occurred in its measured windows, so unlimited precaching is not justified.

Bob New45f:3,447 measured rows and two sampled positions. Bob Old45f:3,582 measured rows, one sampled position. Original pause retains course/mission/score and bottom map; Controller return preserved all main button frames. Original resolution switched actual geometry to256x192 with320x240 centered output, and Native/Wide restored400x240/projection0.8, verified in captured RAM.

The v1 `.upper` CPU-framebuffer readback disagreed with UI colors/rows in Original mode; its pixel margin check **failed** and is not accepted as coherent LCD evidence. The new v2 diagnostic reads the synchronized final GPU target instead, preserving the `.render` ABI. Candidate d1d38fa9ae9df88b builds/audits/packages with bounded384,000-byte temporary linear scratch and failure propagation. Runtime orientation/color/margin validation remains pending. No visual renderer fix is inferred from the bad diagnostic.

Additional analyzed raw series (normal flow, Whomp Old/New, Bob Old/New) were retired with per-attempt hashes. Material register replay C++ fixtures/reports remain reproducible after raw retirement. Diagnostic-only captures are separately classified and excluded from the thirty measured samples.


2026-10-18 CPU-only preload evidence: the saved-capture wall adapter now has a
complete-registry reference and scene-owned cache. In ten queries on that one
saved scene, cached contacts/corrections exactly match the ARM reference.
Preload retains451entries; ARM storage is181392bytes. Geometry reconstruction
reads fall from1245per uncached query to4per cached query, excluding unchanged
contact revalidation on hits. This is a bounded work-count improvement, not an
FPS measurement and not the final ten-scenario/three-capture gameplay matrix.
Source/receipt: `build/movement-adapter/capture-ground-1792330718287286000/report.json`.
No production candidate or emulator/hardware performance acceptance changed.


2026-10-18 unified-backend CPU evidence:420actual host N64 solver steps now use
original DS ARM ground/ceiling/contact/filter queries from the verified saved
physical RAM snapshot. Eight directed wall positions run; one additional
no-floor spawn is explicitly omitted. This remains a frozen single-world test,
not live gameplay or a final scenario-matrix result. Per-tick instead of
per-subquery wall registry/geometry refresh preserves all recorded solver
positions/actions and directed results. Registry checks72336→10320(-85.73%),
prism reads13297→2961(-77.73%); original ground/ceiling/contact/filter counts do
not change. Work-count improvement is not a physical time/FPS estimate.
Before: `build/movement-adapter/saved-world-session-1792332129564594000/report.json`.
After: `build/movement-adapter/saved-world-session-1792332324797190000/report.json`.
No new raw dump was created or deleted; the user's original dump is preserved.


2026-10-18 native-world lab now executes in isolated New-model Azahar with the
original DS collision source instead of converted mesh. Attempt
`build/movement-lab/attempt-1792332884628734000` completes the four-character
presentation script without a logged native/emulated fault;1016native ticks
are recorded. Animation/footstep/voice API/PCM submission audit passes. This is
functional integration evidence only. Its55.608-second supervisor duration
includes startup and teardown and is not a frame-time/FPS benchmark. No final
same-binary ten-scenario RAM/performance campaign or physical acceptance is
closed by this run; no raw VM capture was created or retired here.

2026-10-18 Old-model native-world lab attempt1792335062852235000: two complete
process VM captures, Mario N64 then Luigi Hybrid after held-jump delegation.
`ram-audit.json` validates executable/anchors/owners/allocator lists; both
114336816-byte raw files were retired after analysis, with receipts. Original
user captures preserved. Main integrated candidate remainsc8126d8431f00a80.

Pre-capture timing rings contain947 and878 usable scene3 rows respectively.
WorkerFrameBegin means11.737/11.049ms, WorkerCompose18.402/16.227ms,
WorkerDraw2.924/2.920ms, MainCompiledFrame0.769/0.723ms. Scopes overlap; do not
add them or interpret these host-emulator waits as physical CPU/GPU times.
Resident path still ends an empty acquisition list before SYNCDRAW FrameBegin,
so synchronization/queue ownership needs directed review before any change.
No performance gain or60FPS claim follows from these observations. No final
same-binary matrix case is closed by this laboratory run.

2026-10-18 queue boundary review and measured lab overhead fix:
- The earlier empty-acquisition-list observation applies to the direct-arena
  path; the sampled prepared-mesh calls pass direct_token=0. Do not attribute
  the lab stall to that unused branch. The prepared mesh separately drains
  writable buffers; current evidence does not justify removing that barrier.
- Complete integrated candidate43f004df3b3bd639 adds observations only at the
  real C3D queue wrapper (slots41synced begin,42unpaced begin,43explicit-flush
  end,44full-flush end), preserving exact calls/flags/order. Old castle/Yoshi
  attempt1792335693806552000 completes3captures,3578steady rows, all400×240
  and projection0.8. Synced begin7.880ms, unpaced0.003ms, explicit end0.322ms.
  Full-flush end not observed. These are overlapping guest-emulator intervals.
- CUA entered the title and moved/jumped Yoshi. Three distinct RAM positions
  validate; manual notes were written after the preserved baseline analysis,
  so `queue-analysis.json` explicitly supplements that report's older
  manual_input field. An attempted report rewrite was refused by its guard.
- The lab itself repainted and published320×240pixels every input update.
  Retaining the image until mode/character/status changes removes that known
  redundant work. New labbuildnative-world-1792335807525616000 and
  attempt1792335829788294000 pass both complete RAM audits and interactive
  changing-mode/character/held-jump/dump/resume checks. Composition means
  N64Mario18.402→1.911ms; HybridLuigi16.227→1.929ms. Native draw remains
  approximately2.9ms. These are two interactive windows, not identical input
  replay or physical timings. No whole-game60FPS guarantee follows.
- All3main+2lab rawVM captures were analyzed and retired with exact hash
  receipts; reports/trace/anchors/render data and user originals retained.

2026-10-18 normal camera HUD integration: Oldcastle21c4eeb0 and NewBob754d93d2
each complete3capture runs with real input. RAM validates Native400×240/Wide
and original model/control state. Their3579/3167steady rows respectively show
HUD preparation0.638/0.327ms, native overlay draw0.097/0.070ms. Models/scenes
and input windows differ; no controlled optimization percentage or physical
FPS claim. Exact-byte matching replaced a redundant64-bit hash prefilter.

DS OBJ draws decrease25→9in the observed normal-HUD state, plus one native
quad. `.render` LCD readback contains dump progress during capture; it is not
reliable game-bottom proof. New explicitly sized `.lower` reads the completed
tracked GPU game target. All3NewBob snapshots match the3712opaque native
camera pixels exactly. VM/allocator analyses pass;6new rawVM files retired
with receipts. No final10scenario campaign or physical acceptance is closed.

2026-10-18 all-camera-state candidateebc57e0e12111ceb Oldcastle attempt
1792339119063382000:3complete captures,2554steady rows,1sampled character
position. This is a UI-state regression, not a new multi-zone performance case.
The captured states are Standard normal, Standard pressed-left and Touch zoom.
Native pixel proofs are3712/3712/944opaque pixels with zero mismatches; CPU/GPU
textures and final synchronized game targets agree. Dual-hand small controls
and basicCirclePad movement were observed after the baseline; no extra RAM
capture is claimed.3new rawVM files analyzed/retired; original captures remain.
The definitive same-binary ten-scenario matrix and physical60FPS remain open.
