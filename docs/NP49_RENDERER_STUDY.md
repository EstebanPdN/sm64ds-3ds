# NP49 renderer study and SM64DS integration

Research date: 2026-10-19, America/Los_Angeles. Target: the latest local
integrated SM64DS ROM-only build, not the historical castle viewer or MK64.

## Evidence and coverage

Read the complete [Code Atlas NP49 chapter](https://code-atlas.estebanpdn.chatgpt.site/nintendo-3ds-manual.html#np49)
and inspected the implementation paths below. The chapter's central finding
is correct: native execution, prepared resources, reuse, visibility, PICA
submission and explicit quality policies together explain viability. A shader
alone does not explain the complete frame budget.

The source study covers the graphics/performance architecture and relevant
fork differences. It is **not an exhaustive audit of every source line,
dependency, game system or public 3DS port**. Thirteen direct public forks were
fetched and their observed revisions and differing paths recorded in
`build/np49-research/fork-source-inventory.json`. Most share substantial ancestry;
file differences against a newer main also include missing later changes and
must not be described as independent innovations.

| Source | Immutable revision | Scope |
| --- | --- | --- |
| Original dudewhat import | `9870590b52b720e0bb5f7bf950dd2355f6917497` | Historical shader/thread/patch ancestry |
| MiniBandit published v0.5.2 | `046169ee34be8eb8cd94e241083a69f58650e8b4` | Release baseline; distinct from main |
| NP49 development snapshot | `f89d817ee44b1781bbe4e08dd853b92b38233376` | Main renderer, shaders, resources, animation, visibility, scheduling and Citro3D patch |
| Current MiniBandit main | `694a3c08750c2cf274aa403952588dab65b00576` | Delta from NP49 only changes bootstrap/release builder, not renderer |
| DarthMDev Old scheduling fix | `68f727ad66b3a73266455fd9032eac6437b553de` | Actual merged fix, not default fork HEAD |
| Codiak540 | `12c8844ee9740890c6ba96e93982b9c1fd22d321` | Quality/flush experiments and source delta inventory |
| Epic0522 MK64 | `c7d3da5e864b443a69a53cc98fa24d0a14dbbac1` | Separate Fast3D/Citro3D adapter, native geometry and texture/state caches |
| SM64DS vendor | `88725627c529e5d9db57abc5544513d8741fa07d` | Pinned immutable vendor, with this project's generated overlay |

The latest local candidate at intake was `a2a557f5b08a5050`. Existing staged,
unstaged and untracked development was preserved. New changes are generated
through the existing integrated overlay. No vendor reset or release was made.

## Identity, performance claims and attribution

MKW-3DS is an ARM native reconstruction using Kinoko for reconstructed physics,
plus its own asset conversion, presentation, audio and integration. It does
not execute the unmodified Wii PowerPC program or emulate the whole Wii.
[README and dependencies](https://github.com/MiniBandit/MKW-3DS/tree/f89d817ee44b1781bbe4e08dd853b92b38233376).

The README credits Epic0522 with lighting, kart shadows, water, effects and
menu highlights, and DarthMDev with the Old drawing fix. No separate verified
Epic Wii implementation is assumed. Epic's MK64 repository is a different game
and graphics adapter. Its results cannot be used as measurements of MKW.

The reported New 2D ~60, New stereo ~30 and Old ~30 FPS are maintainer claims,
not new measurements made here. They do not establish perfect operation of
every track, effect, transition and mode. The Old profile deliberately changes
visible workload. Source inspection establishes mechanisms, not console FPS.

The [NOTICE](https://github.com/MiniBandit/MKW-3DS/blob/f89d817ee44b1781bbe4e08dd853b92b38233376/NOTICE)
distinguishes unlicensed original imported code from MIT continuation changes.
This integration is independently written against SM64DS's existing API; no
MKW renderer, shader or copyrighted game resources were transplanted.

## Mechanism-by-mechanism findings

| Mechanism | What MKW actually does | SM64DS equivalent and decision |
| --- | --- | --- |
| Native execution | Native reconstructed systems and Kinoko; conversion before play | Already native ARM game/providers; preserve DS timing and current movement modes |
| Asset cooking | GMDL/T3X, little-endian records, indexed geometry, target textures | ROM-only preparation and packed container already present; a new offline mesh schema would need DS command/material coverage and format versioning |
| Vertex representation | Base 44-byte vertex, 16-bit indices; exact full-attribute deduplication, ~60,000 vertex partition threshold | Existing split position/attribute/index buffers and compiled BMD lists; preserve UV seams, matrix domains and DS generated coordinates |
| Resident geometry | Static model VBO/IBO allocated and flushed on load | `prepared_mesh::Composer` caches topology; `mesh_prepare` reuses unchanged topology and retains texture owners |
| GPU deformation | 57 float4 bone rows = 19 affine palette slots; cooker handles envelope mixes | Already 16 full clip matrices = 64 float4 object rows, preserving DS projection/fixed arithmetic; keep this ABI |
| Normal lighting | Lit shader keeps normal stream, cofactor/sign inverse-transpose direction and normalization | DS signed-normal/light arithmetic is different; existing unified shader implements DS rules, so replacing it with Wii lighting changes output |
| Pose reuse | Pose versions, shared position+matrix transforms, normal representatives; separate full-attribute index reuse domains | Immutable prepared frames and HELD reuse already exist; no arbitrary animation-rate reduction added |
| State reuse | Adjacent complete material equality, plus library-level register/uniform shadows | Existing material cache retained; add palette-slot and texture-dimension reuse inside uninterrupted unified draw scope |
| Visibility | Model bounds, 128-triangle static blocks, 384-index skin bounds, at most four visible ranges preserving order | Existing source visibility plus mixed CPU/PICA clip classification; additional object bounds require animated/eye coverage evidence |
| Fragment behavior | Six TEV stages and supported lighting/material recipes, not GLSL fragment shaders | Preserve DS alpha, stencil IDs, fog and ordering; fix invalidation after the GPU highlight pass |
| Dynamic data publication | Explicit data cache maintenance and counters | Existing flush/drain paths preserved; do not adopt broad range merging without all producer/consumer boundaries |
| CPU/GPU overlap | Deferred double-buffered lists, copied draw parameters, render owner and auxiliary worker | SM64DS already has presentation worker, helper, async audio and immutable frame ownership |
| Queue retirement | GPU completion is separate from render-thread completion | Existing successful FrameBegin drains before overwrite; keep strong frame/resource references |
| Old profile | Nearer LOD, limited particles, animation stepping, reduced bloom, no shadow volumes | No visual quality reduction introduced; original effects and simulation remain requirements |
| New profile | CPU/L2 mode and different worker affinity | Existing CPU mode remains; changing affinity needs scheduling/audio traces, not assumption of unlimited cores |
| Timing | Simulation cadence separate from presentation/stereo | Existing render60 presents intermediate views; it does not establish 60 original simulation updates |
| Audio/I/O | Deadline-driven audio, bounded streaming and preprocessing | Current audio issues remain open; uniform suppression does not claim to solve crackling |

Primary implementation references:

- [Model loading, persistent buffers, material reuse, culling and submission](https://github.com/MiniBandit/MKW-3DS/blob/f89d817ee44b1781bbe4e08dd853b92b38233376/src/renderer.cpp).
- [Actual PICA vertex shader](https://github.com/MiniBandit/MKW-3DS/blob/f89d817ee44b1781bbe4e08dd853b92b38233376/src/mkw.v.pica).
- [Visibility bounds and ordered range merging](https://github.com/MiniBandit/MKW-3DS/blob/f89d817ee44b1781bbe4e08dd853b92b38233376/include/mkw/visibility.hpp).
- [Full-attribute draw-index reuse](https://github.com/MiniBandit/MKW-3DS/blob/f89d817ee44b1781bbe4e08dd853b92b38233376/include/mkw/skin_draw_reuse.hpp), [shared CPU positions](https://github.com/MiniBandit/MKW-3DS/blob/f89d817ee44b1781bbe4e08dd853b92b38233376/include/mkw/skin_positions.hpp), and [animation](https://github.com/MiniBandit/MKW-3DS/blob/f89d817ee44b1781bbe4e08dd853b92b38233376/src/animation.cpp).
- [Citro3D state/uniform shadow patch](https://github.com/MiniBandit/MKW-3DS/blob/f89d817ee44b1781bbe4e08dd853b92b38233376/patches/citro3d-hardware.patch).
- [Deferred render ownership](https://github.com/MiniBandit/MKW-3DS/blob/f89d817ee44b1781bbe4e08dd853b92b38233376/src/render_thread.cpp) and [job worker](https://github.com/MiniBandit/MKW-3DS/blob/f89d817ee44b1781bbe4e08dd853b92b38233376/src/job_worker.cpp).
- [Model cooker](https://github.com/MiniBandit/MKW-3DS/blob/f89d817ee44b1781bbe4e08dd853b92b38233376/tools/cook_model.py) and [world mip cooker](https://github.com/MiniBandit/MKW-3DS/blob/f89d817ee44b1781bbe4e08dd853b92b38233376/tools/cook_world_mipmaps.py).
- [Main quality policy](https://github.com/MiniBandit/MKW-3DS/blob/f89d817ee44b1781bbe4e08dd853b92b38233376/include/mkw/render_quality.hpp).
- [Epic's separate MK64 Citro3D backend](https://github.com/Epic0522/MarioKart64-3DSPort-Complete/blob/c7d3da5e864b443a69a53cc98fa24d0a14dbbac1/platform/3ds/source/gfx_citro3d.cpp).

## What the forks add

The DarthMDev scheduling patch equalizes Old job/render priorities on the shared
core and introduces periodic timeout/yield waits. The condition loops can still
wait indefinitely if no completion arrives; the commit's language about a
guarantee against every deadlock is stronger than the implementation proves.
SM64DS already gives its helper/render workers the same relative priority and
audio a higher one. Copying the literal priority values would not reproduce
the same dependency graph.

Codiak's inspected profile uses LOD distance 150, 16 particles and animation
step 5, versus main's 1500, 48 and step 4. It additionally explores racer bounds,
VRAM texture residency and flush batching. Its `FlushBatch` aligns cache lines,
keeps 64 ranges, merges gaps up to 8 MiB and attempts a process cache flush with
GSP fallback. A wide merged interval may clean much more memory than was changed;
it is not automatically better for this DS pipeline. Neither cache cleaning nor
double buffering by itself proves safe reuse before GPU completion.

The PAL branch includes regional-disc/build metadata. The retail-fixes branch
has a substantial distinct rendering/material history; inventory and selected
renderer paths were inspected, not a full game acceptance audit. The other
forks' observed heads and diff inventories are retained without assigning
unsupported performance advantages. No claim of thirteen independently
optimized engines follows from thirteen repositories.

## SM64DS path and implemented change

Current flow: original game state → compiled BMD/DS graphics preparation →
immutable prepared frame/topology/texture owners → mixed native-indexed and
CPU-clipped spans → one unified PICA program → ordered DS material/shadow/fog
passes → GPU/PPU composition and two displays. Audio and simulation retain
their existing scheduling and timing.

`mesh_bind_batch` previously uploaded every matrix and texture dimension when
moving to another run, even when values in some palette slots were unchanged.
The existing same-run fast path only avoided immediately repeated run IDs.

The new independently implemented cache compares all 16 fixed-point matrix
words per slot before conversion/upload and compares both texture dimensions.
It is bounded to 16 slots and occupies 1,072 bytes on the built ARM target.
It never caches by a recyclable pointer or matrix ID. It resets at every
`UnifiedDrawScope`; non-unified/out-of-scope calls retain the original path.
CPU clipped spans and eye changes only alter disjoint registers within that
scope. Lighting, material, draw order, indices and visible geometry are kept.

The existing uniform-vector counter now counts actual emissions. New request
and hit counters make eliminated work observable. Set
`PLAYER_MESH_UNIFORM_REUSE=0` before renderer initialization for the reference
path. This is a developer environment switch, not a new player-facing setting.

Also invalidate the adjacent material cache when GPU highlight rendering
overwrites TexEnv0. Otherwise an equal following material could inherit the
highlight combiner. This is an invalidation correction, not a quality change.

Files: `source/integrated_native/mesh_uniform_cache.hpp`,
`tools/prepare_integrated_mesh_uniforms.py`,
`tools/test_integrated_mesh_uniforms.py`, with integration in the existing
prepare/build drivers. Pinned vendor files remain unchanged.

## Validation and practical limits

The host gate extracts the real original and generated `mesh_bind_batch`
functions, records uniform values at every draw boundary and compares their
entire uniform histories plus lighting state. It exercises 60 changing poses,
both source representations, unified/reference modes, scope boundaries,
disabled optimization, repeated/changed slots, dimensions and integer extrema.
Clang warnings-as-errors, AddressSanitizer and UndefinedBehaviorSanitizer pass.

441,600 comparisons agree. For the synthetic enabled/unified subset,
1,594,080 original float4 writes become 1,022,639 (35.85% fewer). This is a
synthetic work-count result, **not a frame-time or FPS gain**. The pre-existing
same-run optimization is present on both sides of that comparison.

Full integrated ROM-only build, existing build gates and native provider/data/
ELF/3DSX audit pass. Candidate ELF:
`eb76eabaf6f4e95cb4b840e155f793dfca0ac3b282dfdbb3855a9cc2cbe87c07`.
3DSX: `d27d420997da51068a16ba61f10aa747605d2344e8052025c9120c6663d04d34`.
Locally packaged CIA:
`20828cf3d79515868fb4188007bbda713f25893b26f2de2de63a41239c9c75df`.
The package verifier matched its exact NCCH content and candidate. Nothing
was published. No ROM/prepared assets/DSP are in the delivery package.

Emulator acquisition results and real-game reuse counters are recorded in
`build/np49-research/validation.json` when the isolated sessions close. Physical
Old/New frame time, sound quality, stereo and broad scene/lifecycle acceptance
remain unverified. Concurrent unrelated emulator activity also precludes a
meaningful host wall-time comparison.

## Remaining optimization work

1. Obtain same-scene physical baseline/candidate traces: complete-frame median,
   p95/p99, submit/queue/wait time, geometry/texture bytes, memory and audio
   underruns. Old, New 2D and New stereo are separate workloads.
2. Measure repeated compiled-BMD decode/topology misses and dynamic attribute
   bytes. Extend reuse only with complete source/palette/lighting identities;
   keep lifecycle owners and fallback behavior.
3. Evaluate conservative object/submesh rejection before deformation with
   animated bounds, DS clip semantics and both-eye visibility. Do not remove
   gameplay updates because an actor is offscreen.
4. Profile helper/render/audio queue contention before changing New affinity or
   waits. Avoid importing MKW's priority constants without matching ownership.
5. Consider prepared target textures/mips only after image comparison and
   schema/identity work. DS palette animation and capture textures remain dynamic.
6. Treat reduced LOD, animation stepping, particles and removed passes as
   explicit visual policy changes. None was enabled by this implementation.

The implementation applies one concrete transferable mechanism and repairs
one related correctness boundary. It does not claim that every technique in
NP49 has been newly implemented or that a full renderer rewrite is complete.

## Closed Azahar validation (2026-10-19)

Both isolated candidate sessions closed with `benchmark_three_complete_captures`
and no detected emulator fault. The closed-acquisition analyzer validated three
complete captures per session: Old/castle-Yoshi (3,518 steady rows) and
New/Bob-Mario (2,395 steady rows). Both owned emulator processes were absent
when validation finished. The unrelated MK64 emulator was left untouched.

Exact candidate ELF symbols and ARM DWARF layout locate the cache counters in
hash-verified VM captures. At the final capture, the Old session avoided
859,728 of 3,821,943 requested float4 uniform writes (22.49%); New avoided
1,900,853 of 9,347,456 (20.34%). These are cumulative operation counts in two
different scenarios, not frame-time improvements, hardware measurements or an
Old-versus-New speed comparison. No disabled-cache emulator A/B was performed.
`build/np49-research/collect_validation.py` reproduces the checked counters in
`validation.json`; the independently extracted ARM layout is in
`cache-arm-layout.json`.

All 47 files in the frozen candidate's options source directory match the
current workspace byte-for-byte. The current orange Continue/Controller/
Options/Quit pause menu and retained native map were visually observed in the
New session. Options submenu interaction was not confirmed before the timed
inspection ended. Old castle gameplay was visually observed and its screenshot
is `build/np49-research/old-castle.png`. Input and these limited observations do
not establish full UI, camera, course, audio, stereo or hardware acceptance.
