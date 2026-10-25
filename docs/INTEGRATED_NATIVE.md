# Integrated native backend and 60 Hz experiment

The full native platform from Sébastien Breton is vendored with its MIT notice
and pinned file hashes in `reference-native.lock.json`. The decomp dependency
is pinned independently. Existing original runtime sources, ROM, archive,
private saves, diagnostics and historical CIA remain separate.

## Install from a single ROM

The ROM-only candidate `9951be9b86f38939` accepts the original EU ASMP revision 0
with any `.nds` filename directly inside `SD:/sm64ds/`. Extension matching is
case-insensitive and preserves spaces and Unicode names. Discovery reads only
32 header bytes per candidate, holds a constant number of path buffers and
ignores directories and symlinks. The complete SHA/CRC check rejects damaged
copies and continues in deterministic filename order before any cache writes.
Renaming a valid ROM retains its existing versioned cache. Its installer checks the complete ROM and
prepares 2,450 files using a 64 KiB scratch buffer. Each result is checked
against the independently extracted pinned reference before ARM compilation.
The deliverable SD tree contains only the 3DSX. ROMs, extracted data, firmware,
saves, settings and RAM captures are excluded from the package.

The loading screen decodes the original castle, wallpaper, logo, menu frame,
star and both fonts from the ROM. The CIA embeds offsets and checksums, without
those image payloads. Python generates only the small original progress fill
and percent symbol. The painter uses the existing framebuffers and allocates
no GPU textures or per-frame heap storage. It redraws after a meaningful
percentage/phase change, at least 250 ms apart, and polls input at each callback.

CFGU selects English, Spanish, Portuguese, French, Italian or German. Other
values and initialization/read failures select English. The detector balances
its own CFGU reference and does not modify console or game settings. All phases,
counts, errors and instructions use the same dictionary; UTF-8 measurement,
wrapping and painting share one Latin1 decoder. The 166 retained original glyphs
include their accents. A missing-ROM screen draws its own small accented font.

Host ASan/UBSan verifies all six languages and reference lifetimes, 300 strings,
7,278 glyph checks, 90 painted states, malformed UTF-8 and bounded long text.
The artwork decoder passes 2,518 checks against exact Python-decoded pixels.
It retains 329,344 bytes, peaks at 772,685 bytes and releases all owned blocks.
Boot strings and vectors explicitly use libc because the port's global C++
allocator belongs to the original DS heap, which does not exist at that stage.
The exact ARM/3DSX pair and complete CIA content/profile are verified separately.

Acquisition `attempt-1792127820692207000` completed the first preparation in
Azahar Old with no initial cache. All 2,450 files, 15,686,745 bytes, match the
independent extractor and their frozen SHA256/CRC32 values. The original ROM
is unchanged, the complete marker is correct and no partial files remain.
The log before game startup reports zero live boot bytes/blocks. A complete
mapped native capture in scene 3 independently reads those same zero counters
through their exact ELF symbol. It also checks 11,204 live script bytes, 61 typed
pointers and 392 original initializer writes. The 120,627,200 readable mapped
bytes may contain aliases; they are not a unique physical RAM footprint or a
full atomic system snapshot. The original title, file menu and intro were observed,
followed by controllable Yoshi moving from sand to grass and jumping visibly
above his shadow. The acquisition closed after 736.07 seconds without a recorded
emulated CPU/memory fault. This is a short gameplay check, not full-game parity.

A preexisting emulator-only DSP component remains an explicit test-environment
limitation: the physical Home Menu DSP extraction and installation still require
a console. This check does not establish sustained physical Old3DS FPS or long
session leak freedom for the complete game. See [ROM_ONLY_INSTALL.md](ROM_ONLY_INSTALL.md).

## Reproduce the baseline

```sh
cmake -S . -B build/integrated-native-driver
cmake --build build/integrated-native-driver --target sm64ds-integrated_3dsx
```

This builds native ARM game, GPU rendering/composition/shadows, original SDAT
audio with DSP output, actor/level/overlay lifecycle, saves and analog controls.
The candidate selects free C-stick yaw/pitch. The build gate compares all 11,204
packed script bytes with the private EU ROM while normalizing only 61 proven
pointer fields, walks the 64 intro commands, checks linked providers, and
independently reproduces the exact 3DSX from the ELF. Host ASan/UBSan exercises
the actual settings/camera source. No emulator is automatically launched.

Baseline `efaf63f98abc258a` reached the original castle intro and controllable
Yoshi in Azahar 2126.1.2, Old model, with no emulated CPU speedup. Movement and
an airborne B jump were observed through normal UI input. Exterior cadence
was about 29–30 displayed FPS with 120 VBlank hooks per 60 original updates.
Acquisition `attempt-1792108106156161000` preserves logs, an emulator full-system
state and observation receipts. The CST decompresses completely to 183,367,901
bytes. A decoded archive is not yet a verified native virtual RAM map; the
tentative four-anchor `.data` reading is explicitly inconclusive.

## Render-only 60 Hz candidate

```sh
cmake --build build/integrated-native-driver --target sm64ds-integrated-60_3dsx
python3 tools/gate_integrated_render60.py
```

`tools/prepare_integrated_render60.py` creates a private generated derivative
under `build/integrated-native/render60-source`. It does not edit the vendored
reference or original decomp. Each adapted file has a recorded hash. The
additional original code lives in `source/integrated_native/render_60.hpp`.

The original Stage updates at 30 Hz and has an intervening held display frame.
The baseline skips GPU work when that image is unchanged. The experiment uses
two immutable source poses to emit a matrix midpoint on the first interval,
then the exact current pose on the next. Actor behavior, collision, input,
timers, cutscene commands and audio are not updated a second time. HUD/2D
animation retains its original cadence. This adds half a source step of visual
latency; it is not a 60 Hz gameplay-logic conversion.

Admission pairs each compiled span with the previous span of the same
render-list actor (unique ID + address) and the same display-list layout.
Distinct actors never share a key, so identical models of two actors are never
blended together. An actor entering or leaving the view, spawning or
despawning changes only its own spans: those keep their exact current matrices
while every matched actor still blends (`partialFrames`, `unmatchedSpans`).
Until 2026-10-23 any such change refused the whole midpoint; in the Bob-omb
New acquisition that cost 9.7 % of updates. Unowned legacy particle/water spans
keep their current matrices. Repeated identical lists within one actor remain
exact because their submodel identity is ambiguous. Scene/camera cuts and
render-list order changes still reset history. A frame with no matched actor,
skipped source epochs, exhausted slots, allocation failure and other
unsupported paths use the original image; refusals are counted by reason
(`refusedNoSource`, `refusedNoMatch`, `refusedFootprint`, `refusedSlots`).
Endpoint selection takes precedence over optional interpolation when the
scheduler is behind.

## Display > Lock FPS

`Off`, `30` and `60` change presentation cadence only; the original game keeps
its 30 Hz update clock and real-time catch-up in every mode.

| Value | Presentation |
| --- | --- |
| 60 | Render60 midpoint plus exact image, Citro3D LCD synchronization |
| 30 | Exact image once per update; the held VBlank skips GPU work (`locked30`) |
| Off | Midpoints without waiting for the LCD; the latest finished image wins |

`settings.ini` stores `[display] fps_lock = auto|off|30|60`. `auto` (default)
resolves at boot through `APT_CheckNew3DS`: Old 3DS 30, New 3DS 60. A failed
query uses the Old value. The target remains a stable 60 on Old 3DS; its
default changes to 60 once that is measured on hardware.

The experiment blends final projected matrices. It conservatively refuses
basis changes beyond 15 degrees, large scale changes, sign changes or a
degenerate midpoint. Small matrix blends are still an approximation to rigid
pose interpolation; a complete implementation should interpolate rotations,
translation and scale before projection. The two slots admit at most 1 MiB of
logical snapshot contents each. This is not a strict retained-RAM bound: vector
capacity and shared topology/texture leases also occupy memory. GPU resource
fences and cache maintenance remain in force.

ASan/UBSan tests exercise matrix midpoints versus exact endpoints, source
immutability, busy-slot ownership, widened signed arithmetic, topology changes,
actor identity/order changes, repeated submodels, held epochs, large rotations,
renderer-result accounting and reset/fallback. The readiness receipt pins the
ELF, exact source hashes and linked cut/actor hooks before launch.

## Code Atlas application

The local October 2026 Code Atlas manual was read, especially NP07–NP09, NP12,
NP15, NP18 and NP36. Its relevant requirements are applied narrowly:

- Preserve the original simulation clock while adding optional display images.
- Keep immutable source identity and GPU buffer lifetime explicit.
- Preserve supported cache flushes and GPU completion ownership.
- Count midpoint/endpoint selections, canceled endpoints and accepted/rejected
  renderer jobs separately; report result-handoff mean/p50/p95/p99/max.
- Compare named workloads against the unchanged baseline; reject correctness
  regressions instead of combining unrelated optimization switches.

`Render60 jobsAccepted` records renderer results consumed by the main thread
after waiting for the worker. Its timestamp is a job-result handoff, not the
instant of worker submission, GPU completion or physical LCD presentation.
Older logs call this same timing `submissionMeanMs`; the analyzer explicitly
classifies both versions as handoff timing. Endpoint selection and accepted
endpoint jobs have separate counters; neither confirms LCD consumption.
Physical display cadence requires its own evidence. Emulator timings cannot
establish physical Old 3DS performance. Fallback can reduce interpolation
coverage; the counters expose that instead of claiming constant 60 FPS.

Public reference: [Code Atlas Nintendo 3DS manual](https://code-atlas.estebanpdn.chatgpt.site/nintendo-3ds-manual.html).
Credited platform: [sm64ds-3ds](https://github.com/SebastienBreton/sm64ds-3ds).

## Acquisition and evidence

`tools/run_integrated_native.py` checks the closed candidate and readiness,
copies a portable Azahar bundle/profile and private SD payload into a unique
attempt, and launches only that owned process. It preserves the exact ELF,
image and retained files with hashes. There is no global emulator kill and no
personal save/profile reuse. Only the locally available DSP firmware is copied.

The first prototype `8744d3c4c6fbc02b` reached water and pause/resume but was
rejected as the final solution: changing legacy particle spans disabled whole
frame interpolation, and scheduler skipping could replace an endpoint before
selection. Per-actor matching and endpoint priority address these mechanisms.
`af7fe6a5c86b253a` improved steady handoff cadence to around 59.8/s, while
Azahar App FPS still varied. The following `5176c95aa584b33d` restored Citro3D
LCD synchronization, keeping the original software/simulation clock. It closed
after 902.53 seconds with no emulated CPU/memory fault. A six-second Up input
advanced 178 original game updates; jump and exterior traversal were observed.
Its movement samples showed App FPS 48–60; this is not a stable 60 FPS pass.

The corrected candidate `a59f000b1b7106b1` additionally guards ambiguous
submodels and unsafe matrix blends, fixes held-source epoch tracking, and
records endpoint job results separately. Its exact readiness gate passed
before the acquisition. It closed after 535.34 seconds with no emulated CPU or
memory fault logged. Original intro, Yoshi traversal, pond entry with surface
particles/translucency, pause and resume were observed. App FPS samples were
19–20 during parts of the intro, 52–60 during exterior movement and 47–60 in
water. These are isolated samples, not percentile distributions or hardware
acceptance. `observations.json` and `render60-analysis.json` retain their scope.

`tools/inspect_integrated_state.py` validates the 256-byte CST header and the
complete ZSTD archive without restoring state or changing RAM. Each receipt
pins its ELF and state hashes. The intro capture for this corrected candidate
decodes completely to 183,388,724 bytes. These are full emulated system states,
not just screenshots; decoding a Boost archive still does not establish the
native process virtual memory map. The tentative older four-anchor mapping
remains explicitly inconclusive and is not used for live-variable diagnosis.

## Native virtual-memory and timing capture

Candidate `f90d66d5b27311d9` adds an opt-in native capture. Start its acquisition
with `--native-diagnostics`; the runner creates a profile-local
`native-diagnostics.enabled` marker. Select (N in the test keyboard profile)
then drains the presentation worker and records readable CPU virtual memory
below 0x40000000, with every mapped/skipped region and its permissions/state.
Free, reserved and device-I/O regions are not read. A hard byte/region budget,
write errors or incomplete traversal leave the file incomplete; the inspector
rejects it. Original RAM is never patched or restored. Capturing at the frame
hook stabilizes game execution, but GPU/OS/DSP memory and capture I/O metadata
are not an atomic whole-system snapshot; retain CST evidence for that scope.

The initial title capture contains 114,335,744 bytes of readable virtual regions
and reports `data_0208ee44 == 1`, with no live player. Virtual aliases can be
counted more than once; this is not a physical allocation/high-water total.
Anchor addresses come from the running program, rather than from a guessed
archive bias. `tools/inspect_integrated_vm.py` translates those addresses and
compares all 11,204 packed script bytes with the private ARM9 source after
Stage initialization. The expected image includes 61 typed table pointers
and the original constructor's 392 byte stores, including unaligned bytecode
pointer operands. Those stores are evaluated from the pinned constructor;
arbitrary byte differences are not masked. All six previously corrupted
serialized words still match the ROM.

An optional 4096-entry/640 KiB ring captures existing timing counters after
the worker drains. The counters overlap; do not add their intervals or call
them GPU/LCD timings. The ring is written before the memory capture and reset
after capture I/O, so a later trace does not include that write interval.
The first attempted activation used env.txt and was rejected: user builds
deliberately ignore that file. The marker preserves that isolation contract;
diagnostics stay disabled in ordinary candidates unless explicitly enabled.

The acquisition closed normally after 787.29 seconds without a logged
emulated CPU/memory fault. Four complete mapped captures each contain
114,335,744 readable virtual bytes. The title uses cadence 1; the original
intro and playable Stage use cadence 2. One player address is reused by intro
Mario (UID 2) and playable Yoshi (UID 95), confirming the need to compare both
address and unique ID when matching temporal poses. Actual six-second Up
input after closing Controller Modes advanced 180 original updates, with
57.18 result handoffs/s in two fully contained log windows. This is neither
physical LCD timing nor a sustained 60 FPS pass. Select retains its original
input semantics: during gameplay it also opens Controller Modes. Input while
that menu is open is excluded from traversal evidence.

The intro capture contains 212 samples with MainPreparePpu above 25 ms; their
mean is 35.59 ms, including a mean MainPrepareB of 17.24 ms. The counters
overlap with worker and wait scopes and must not be added. Captured Engine B
BLDCNT is 0x2840: alpha mode is selected but no overlay is a first target.
The original affine-overlay path nevertheless recomposes through its alpha
loop and keys the overlay cache on affine motion. These registers and a
retained affine cache alone do not establish which path is currently used;
the later dispatch inspection below corrects that initial working hypothesis.

## Inactive-alpha optimization

The derivative PPU keeps every original admission restriction and resolved
blend state, but uses the existing non-blending overlay loop/cache when
alpha mode has no enabled first-target overlay. Active alpha keeps the
original path. No simulation, quality, framebuffer size, clock or GPU fence
changes accompany this optimization.

`tools/test_integrated_inactive_alpha.py` replays the actual captured 2D
registers, palette, VRAM and OAM against the pinned PPU and the derivative.
Its 64 cases vary affine motion, scroll and active/inactive alpha. Both
implementations admit 48 cases and refuse the same remaining 16; all
12,586,816 output bytes match exactly under ASan/UBSan. Host preparation
times are diagnostic only and do not predict ARM11 or physical FPS.

Candidate `bcc1fbc9bf6af826` includes this one runtime change relative to
`f90d66d5b27311d9`. Its complete ARM build, packed-script/link audit,
ELF-to-3DSX reproduction and readiness checks passed before launch. The
candidate archives the actual adapted source bytes, verification tools and
pixel-equivalence receipt under `source-evidence/`; its closed file manifest
also hashes those files.

Its first title-only acquisition consumed no game input. A hidden Azahar
update dialog was blocking HID; File/Load File followed by Escape exposed
the dialog, and Ignore restored ordinary touch and keyboard delivery. One
verified title-only recovery of the same binary then observed original menu,
intro, Yoshi movement/jump, pause and resume. Four mapped native captures are
complete and script-exact; a complete CST decodes to 184,050,868 bytes. This
candidate did not reach water. The supervisor raised EPERM in its cleanup
after UI quit; the recovered receipt records that failure, confirms the exact
PID/group is absent, and leaves unavailable elapsed time null.

`attempt-1792121137625009000/intro-common-window.json` aligns the same Stage
source-epoch span, 2..908, in the f90/bcc acquisitions. Total MainPreparePpu
per original update is 9.468 versus 9.391 emulated milliseconds. This small
difference does not establish a speedup. Captured game work, idle time and
host scheduling are not identical merely because epoch ranges match.

## Semitransparent OBJ over the affine background

`tools/inspect_integrated_ppu_dispatch.py` derives member offsets from each
exact ELF's DWARF and validates runtime anchor biases before reading the VM.
Both captured intro jobs have no GPU-affine descriptor or resident OBJ plan;
their engine-B candidates are already resolved by the CPU. The historical
GPU-affine cache is at generation 192 and describes three text overlays,
whereas current DISPCNT is 0x40011b03 with two. In both captures the first
native OBJ rejection is a visible mode-1 semitransparent sprite, OAM index 16.
That rejection occurs before the parallel affine attempt. It explains why
the inactive-alpha shortcut does not address the captured expensive route.
The finer optional PPU profiling clock was not enabled, so zero nested
BG/OBJ/resolve counters cannot apportion the whole measured cost further.

The experimental `affine_alpha_objects.inc` reuses the original CPU OBJ scan,
selects the same winning OBJ plane, and resolves winning semitransparent OBJ
pixels against their immediate affine/backdrop target with the original
six-bit blend rule. It merges that plane with the existing text overlay and
retains GPU affine sampling for the remaining map. It accepts only Engine B,
alpha mode with no BG/OBJ first targets or OBJ second target, no windows or
master brightness, and OBJ priorities no farther back than the affine layer.
Text overlays as second targets, bitmap/window OBJ and mosaic keep the full
CPU path. No game simulation, input, audio clock, quality or fence changes.

The producer executes once after wait_idle and the helper-B join; its OBJ
plan pointer stays null because those pixels are already in the overlay.
Its double-buffered output borrows the existing affine raster only until the
next engine-B preparation. Additional logical storage is 638,976 pixel bytes
plus small descriptors and allocator overhead, allocated lazily outside the
game heap; allocation failure returns to the full PPU. This is not a measured
physical-console allocation peak. The GPU overlay upload key now includes
both source pointer and generation, preventing collisions between independent
BG-only and OBJ-combined generation counters.

`tools/test_integrated_affine_alpha.py` compares final 256×192 images with
both ppu_prepare_candidates_cached/ppu_compose_candidates and direct full CPU
scanout. Each of two real intro captures supplies 192 admitted variants and
25 refused cases: alpha coefficients, affine motion/wrap, scrolling, clipping,
OAM ties/overlap, normal versus semi OBJ, affine OBJ, extended palettes, and
transparent affine/backdrop. All 18,874,368 pixels per oracle match under
ASan/UBSan with sanitizer errors fatal. The same gate exercises the actual
upload-key helper through equal-generation producer switches. CPU replay
does not execute PICA texture uploads, prove worker scheduling or establish
physical speed. Candidate `277c9139291b55c4` passes the full ARM build, linked
provider audit, ELF/3DSX reproduction and exact-source readiness gate. Its ELF
adds 17,696 BSS bytes besides the lazy pixel storage. A single new acquisition
with the Old profile closed after 205.15 seconds without a logged CPU/memory
fault. macOS remained locked on CUA observation, so no normal game input or
visual observation was possible. Only the owned emulator group was stopped.
The title-only log is not visual, input or optimization acceptance. The runner
permits one recovery of this closed, hash-verified title-only acquisition
after unlock; a session with consumed HID/gameplay cannot use that exception.

The dispatch inspector now has an explicit `--expect-after affine-alpha`
mode for the next native capture. Besides the current producer descriptor,
it requires the compositor's source/generation key to match the selected
overlay and a successful drained worker. It reports owned buffer capacities
and distinguishes borrowed BG rasters. Its historical CPU regression is
byte-for-byte equivalent to the prior readout, and it refuses a CPU capture
as GPU acceptance. Positive producer/consumer verification still needs a
new RAM capture; these checks cannot establish physical LCD delivery.

Physical Old/New tests, full progression, audible quality, C-stick hardware,
long-session memory behavior and sustained all-game 60 FPS remain open.

## Save persistence evidence

The playable acquisitions did not yet create `user-test.sav`. This is not by
itself a write failure: the save HAL reads a missing cart as an all-FF image,
and the original ReadFileData path supplies defaults without creating a file.
The original menu read command (20) also leaves the file absent. A write must
be explicitly triggered before persistence can be accepted. Stage's pause
save path reaches SaveCurrentFile in PS_Update state 0x0f; level completion,
save dialogs and file operations provide other write boundaries.

`tools/test_integrated_save.py` compiles the pinned HAL and its existing cart
probe plus original menu dispatcher/file operations into isolated host tests.
The current run passes with fatal ASan/UBSan errors enabled: create, replace,
reopen, mirrored blocks and primary-corruption recovery; original blank-slot
initialization, six directed slot copies, three erases, and reload from disk.
Only temporary test files are used. Its source-pinned receipt is retained in
`build/integrated-native/save-host-1792123600999663000/report.json`.

These tests establish host-file and isolated command behavior, not an actual
3DS save/restart. The next gameplay acquisition must select an original Save
action, verify the resulting cart file, and then reload through the original
menu while preserving the pre-restart logs and capture provenance.

The ELF also links the actual save HAL, archive rename/fsync and FSFILE_Flush
implementations. Its libctru debug paths identify packaged version 2.7.0.
The linked rename disassembly and the [tagged libctru implementation](https://github.com/devkitPro/libctru/blob/v2.7.0/libctru/source/archive_dev.c#L875-L932)
handle an existing destination by deleting it and retrying RenameFile. Normal
replacement is supported; this must not be described as an atomic overwrite
or crash-durability guarantee. The local call/byte receipt is retained in
`build/integrated-native/save-sdk-1792123833115966000/report.json`; whole SDK
source-to-binary reproduction and physical filesystem behavior remain outside
that static check.
