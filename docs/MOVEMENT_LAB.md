# Castle movement laboratory

Status: experimental, not accepted. The owner rejected the earlier prototypes
for movement, voice and animation problems. The new native DS-world laboratory
now has a passing four-character Azahar presentation run; Hybrid/DS ability
handoffs and physical hardware remain pending. This lab does not modify the
main game controller menu.

## Modes

The separate 3DSX enters the DS castle exterior through original StartFile.
The lower LCD offers Original, Hybrid and N64, plus all four characters.
Its private save/log paths are `sdmc:/3ds/sm64ds/movement-lab.sav` and
`sdmc:/3ds/sm64ds/movement-lab.log`.

- Original delegates player behavior to DS.
- N64 runs the imported SM64 3DS-port controller with unity factors for Mario, Luigi and Yoshi; Wario retains the original DS speed/jump ratios in both native modes.
- Hybrid uses that controller, with original DS held-jump actions for Luigi and
  Yoshi. Wario uses DS table ratios: speed 3072/3686, jump launch 3358/4096.
- Flower transformations and Yoshi mouth/tongue/egg actions delegate to DS.
  Complete progression and interaction acceptance remain outstanding.

## Movement source and boundaries

The movement source is the local SM64 3DS port, repository
https://github.com/mkst/sm64-port, commit
`3f3c01eae67d6e9f3af89bc8073e37a5d153e00f`. The builder imports 348 actual function
bodies from mario.c, mario_step.c and six action-family files into a disposable
host engine. It imports the original adjust_analog_stick and follows the 3DS
Circle Pad path (raw axes / 2, original axis deadzone and radial cap).

`port_source.py` records original source lines, original/adapted function
hashes and explicit engine boundaries in `build/movement-lab/core/port-source.json`.
Some startup, camera, geometry, animation API and object-dependent functions
remain adapters. This is not a claim that the full original game engine was
ported without changes.

Libsm64 at `fd11813208272b4271d92bd92feb8f3fdbe61be5` supplies the host API,
surface query engine and animation plumbing. Its unmodified source remains in
third_party; it is no longer the authority for the imported movement routines.
Complete original SM64 animation tables are consumed privately for controller
root motion and action timing. DS supplies rendered bodies, camera and audio.

Only the N64 controller displaces the player while it owns movement. The DS
position solver is skipped, while the separate DS terrain/water sensor remains.
Authoritative imported position is committed after DS post-behavior processing.
Dynamic interactions and ownership transitions still require broader validation.

## Audio and body animation correction

The presentation adapter is separate from the movement solver:

- DS walking clip 0x48 and running clip 0x3f loop. Selection follows the imported
  controller's walking/running animation state, including its speed hysteresis.
- Walking playback follows the original DS speed / 8 formula with minimum 0.25;
  running uses the original DS base rate 2.5. Walk/run changes retain normalized
  cycle phase. Original DS ModelAnim keeps the cursor when updating a clip rate.
- Newly selected bodies with an empty frame controller explicitly rebind the
  animation, bypassing the same-file fast path.
- Actual DS Animation::WillHitFrame tests at frames 4 and 19 trigger the original
  DS footstep function func_ov002_020d4c30. N64 skeletal step events are suppressed
  so they cannot produce a second, differently timed footstep.
- Jump events use original DS func_ov002_020e25f0, its live random generator and
  its per-character two-variant jump table. Verified special cases use the DS
  long-jump voice 4, wall-kick voice 0x0c and ground-pound voice 0x15.
- Takeoff audio is emitted once; landing uses original DS func_ov002_020c2f64,
  preserving its landing latch, metal and shallow-water rules.

These changes do not establish complete animation or sound coverage. Water,
slides, attacks, unusual landings and interaction presentation remain incomplete.
The current DS-to-N64 material conversion still needs CLPS validation.

## Build and validation

Run `python3 tools/movement_lab/build_lab.py`. It requires existing integrated
DS link objects, local castle KCL, and the local SM64 3DS-port sources/animations.
Only private derived sources and lab objects are compiled. The output is
`build/movement-lab/sm64ds-movement-lab.3dsx`.

`run_lab.py --replay` runs a bounded scripted movement scenario in a fresh owned
New 3DS emulator profile. `--presentation` exercises slow movement, running,
jumps and body changes for all four characters. Test-only resets/inputs are
explicitly opt-in; normal interactive play has neither. The runner stops only
its own process. ROM assets and local DSP firmware remain private under build.

`compare_trace.py` compares every recorded ARM input, action, timer, animation
and movement result with a host execution of the same imported port controller.
It also compares the DS committed pose, allowing fixed-point quantization.
This detects integration divergence; it is not independent original-game or
physical-console acceptance.

`audit_presentation.py` checks real loaded DS frame lengths, looping flags,
cursor advancement, repeated walk/run cycles and foot contacts for each body.
The diagnostic executable taps the exact stereo PCM submitted to NDSP into a
bounded RAM buffer, writes it after the scripted test, and the audit creates a
local WAV. PCM activity proves output submission, not subjective audible fidelity.
Actual DS voice IDs are logged at the original voice API through a linker wrapper.
The tap is inactive during normal interactive use.

The corrected candidate SHA-256 is
`81c5a5679ab0a3925394831b1d3dc8e348957d02216f763898cda6ae2415813d`.
The owned presentation run completed without a logged emulated fault. Each of
Mario, Luigi, Wario and Yoshi completed at least two walking cycles and six
running cycles, with checked contact frames and two normal-jump voice calls
(actual original DS voice IDs 0 and 1). No empty clip remained. Its 1,017
recorded ARM ticks matched the host controller and committed pose without
mismatches. The NDSP tap retained 43.255 seconds of stereo PCM. This confirms
submitted samples and verified voice dispatch; audible fidelity is not claimed.
The first diagnostic run's empty initial idle and incomplete fixture coverage
were retained under build and corrected before this passing run.

An independent original-DS ARM collision oracle sampled 951 floor queries:
950 hit in both engines, one was missing from the converted mesh, and five
height differences exceeded two world units (maximum 12.1343). Integer triangle
reconstruction and collision semantics therefore still have known differences.

Remaining acceptance includes manual visual/listening review, all movement and
landing transitions, slopes/walls/ledges/water, CLPS/dynamic surfaces, Wario block
breaking, Yoshi progression mechanics, all flower powers, Hybrid held-jump entry
and exit, mode/character changes during actions, and physical 3DS play.
No identical-physics, full-game completion or physical-performance claim is made.


## 2026-10-18 production-adapter preparation

The main port now has a separate Buttons submenu; Control/physics remains
unimplemented there. The local KCL decoder component preserves fractional
coordinates and attributes and validates all1245prisms from the user's saved
physical capture prefix, including two triangles belonging to a moving object.
It is not linked into game movement and does not close the951-query collision
oracle differences above. The earlier integer mesh must not be promoted as an
exact DS collision representation. Next integration must resolve original DS
query semantics, moving transforms and stable surface lifetimes before taking
position ownership from the DS controller. Candidate7183d9ec builds the original
upper-diagram preview mapping; only474bc614 has the directed Buttons/Touch
inspection so far. Hardware/N64/Hybrid acceptance remains open.

## Original collision-query routing gates, 2026-10-18

`source/integrated_native/movement/collision_provider.{h,c}` adds an optional
simulation-thread floor provider to the generated N64 core. It leaves the
unregistered laboratory path unchanged, and a registered miss/error never
falls through to an inconsistent approximate mesh. Host and ARM archives
build. `tools/movement_lab/audit_floor_provider.py` independently executes the
recovered DS ARM ground implementation and routes951queries through the N64
entry points: the earlier one missing hit and five height differences over
two world units become zero mismatches. The helper routes and explicit miss
also pass. The result is in `build/movement-adapter/floor-provider-gate-latest.json`.
This fixture does not implement live actor movement or production surface
storage/property conversion.

`ds_world_query.{h,cpp}` is the next bridge: it constructs the original ground
query, supplies position/actor/probe depth and calls the game's original
24-collider registry. It returns Fix12 height/world normal, both raw CLPS
words, collider/owner addresses, owner ID, triangle and registry slot, with
no heap allocation or I/O. Misses leave caller output unchanged.
`tools/movement_lab/audit_world_ground.py` links an independent ARM overlay
against the preserved collision executable, exercises every registry slot,
checks owner exclusion and raw CLPS, and compares951queries against the
direct static-collider oracle with zero mismatches. See
`build/movement-adapter/world-ground-gate-latest.json`. Test fixture actor
records exist only in Unicorn RAM; no live game RAM is written. DS MMIO is
unmapped. The earlier subset object files have been retired, so this gate
uses the retained executable's verified symbols/instructions and separately
compiled original registry functions rather than stale object-list paths.

The adapter is deliberately not registered in the production controller yet.
The N64 floor API admits planes up to78world units above its input Y; the
original DS ground query has its own admission rules and movers use a finite
probe depth. The high-altitude951-point oracle does not establish matching
near-contact semantics. Next work must explicitly preserve those rules,
complete walls/ceilings and stable surface/contact lifetime handling, and
test moving geometry before changing simulation ownership. The static
24-slot gate does not prove a moving collider's transforms or velocities.

`sm64ds_motion_ground` now makes that admission adjustment explicitly for the
integer world coordinates used by the N64 floor iterator: query throughY+78,
include equality despite the DS static query's strict-above test, and reject
results at/below-11000. All intermediate Fix12 conversions and probe depth use
64-bit range checks before narrowing. Eight flat-floor cases check inputY174,
175,176,177,253,254,255,1000 against the castle's exactY254floor; hits begin
at176as required. Four overflow/below-lower-limit inputs fail without modifying
output. These cases bring the ARM gate to1948calls; they do not establish
sloped-surface admission, moving geometry or production integration.

The static near-contact gate now also checks2067queries over234sloped
triangles (one integerYbelow admission, at admission, and one above). It reports
zero admission differences and4015total ARM calls. Those are CPU semantic tests,
not presentation frames or performance measurements.

`tools/movement_lab/replay_captured_ground.py` adds a second, independent path:
it loads only wholly captured readable mappings from the user's incomplete
physical dump, verifies executable.text byte-for-byte against3f262640 and
cross-checks anchors before linking the new bridge against that game's symbols.
The retained dump is never modified. The original collision instructions run
against this saved state in Unicorn, with separate scratch code/stack mappings.

The saved moving collider's two triangles are water(CLPSword0=4064), not an
ordinary solid platform. A separate `sm64ds_world_water_ground` entry point now
uses the original StartDetectingWater/StopDetectingOrdinary methods. Both
triangles match direct original virtual queries, while ordinary ground excludes
the water. Six controlled offline original-Transform cases (translation,
quarter yaw and pitch on each triangle) preserve normals and match registry
queries. The largest measured difference between the fixed-point ray height
and the reconstructed centroid plane is0.106853world units, retained in the
report rather than described as exact floating-point geometry.

`sm64ds_world_line` now wraps original world segment queries and returns the
full world contact, normal, raw CLPS and owner identity. It rejects zero-length
or overflowing segments before the original midpoint/distance arithmetic.
Twenty ceiling contacts from the saved castle mesh match explicit collider
queries, and four invalid segments preserve output. Latest replay gate70calls:
`build/movement-adapter/captured-ground-gate-latest.json`. It does not establish
N64 ceiling admission, cylindrical wall response, solid platform riding,
surface-cache lifetime, full-RAM consistency, live gameplay or physical FPS.

## Stable contact storage component, 2026-10-18

`source/integrated_native/movement/stable_surfaces.hpp` supplies fixed-capacity,
non-movable storage for N64 surface pointers. Before each tick, the owning
controller must enumerate every retained floor/wall/ceiling/platform reference.
Foreign references reject the tick instead of silently losing ownership.
Contacts issued during the current tick and explicitly retained old contacts
cannot be evicted; unreferenced records use oldest-use replacement. Collider,
actor, owner-ID, KCL, triangle and registry slot are part of the key, so a new
owner generation cannot overwrite a pinned old contact merely by reusing its
address. Reset is explicitly restricted to after the solver is destroyed.
No heap allocation or filesystem access occurs in this container.

`tools/movement_lab/audit_stable_surfaces.py` runs the lifetime test with ASan
and UBSan, including10000churn ticks/50000new contacts while preserving three
old contacts, exhaustion protection, invalid-key/foreign-pin rejection and
explicit reset. A separately compiled ARM object measures256slots at30736bytes.
Receipt:`build/movement-adapter/stable-surfaces-gate-latest.json`.
This is a component gate, not main-controller lifetime acceptance: every
retained N64 pointer still has to be enumerated at the actual simulation
boundary, and moving-platform transforms/property mapping remain separate.
The DS sphere correction API was inspected but is not substituted for N64's
distinct wall/cylinder rules. Ceiling sentinel in the current library is
CELL_HEIGHT_LIMIT=100000, so a future ceiling adapter must also account for
long-ray fixed-point precision, rather than assuming the earlier20-unit
fixture segments establish a full-range ceiling query.

## Actual solver contact enumeration, 2026-10-18

`solver_contacts.h/.inc` is compiled inside the generated headless core TU,
using its actual MarioInstance pool and typed MarioState/Object fields, not
guessed ABI offsets. `motion64_get_contacts` and `motion64_visit_contacts`
enumerate floor/ceil/wall and the object's independently retained oFloor,
plus its platform transform. They do not bind a different global solver state;
invalid/deleted IDs leave caller output unchanged. The permanent N64 water
pseudo-floor is explicitly identified; it must not be treated as arbitrary
foreign cache storage.

`solver_contact_pins.hpp` collects/deduplicates all live instances' contacts
without allocation, keeping platform transform references separately for the
future platform owner. Capacity failure rejects the collection instead of
silently dropping pins. The cache still rejects ordinary mesh-owned references:
this prevents mixing old wall/ceiling geometry with a new cache-owned floor
without an explicit ownership transition.

`tools/movement_lab/audit_solver_contacts.py` verifies the current host/ARM
source manifests and ARM exports, then instruments the modified headless TU,
C fixture and C++ ownership layer with ASan/UBSan. It creates two actual N64
instances, checks all four surface fields/platform deduplication, water
pseudo-floor handling, unchanged global binding, capacity/foreign-owner rejection
and removal of deleted instances. A separate360tick stationary/jump scenario
uses an explicit flat-floor provider with generation changes and cache churn;
maximumY496is read from the test output, and no provider fault occurs. The
normal castle mesh is unloaded for this flat-plane fixture, since leaving its
independent retained wall/ceiling contacts would correctly fail ownership checks.
This is not a whole-core sanitizer claim: imported archive members other than
the modified headless TU are not instrumented. Receipt:
`build/movement-adapter/solver-contacts-gate-latest.json`.

Actual main-game simulation-boundary wiring, wall/ceiling adapters, platform
transform lifetimes and live Original/Hybrid/N64 mode transitions remain open.
No new integrated candidate or physical-performance acceptance follows from
this host/ARM component work.

## Long-ray arithmetic correction and integrated build, 2026-10-18

Extending the saved-capture ceiling segments to the library's100000world-unit
sentinel exposed a real defect in the adapted raycast: all20long-ray heights
differed by more than one world unit from the short-ray contacts, with maximum
difference23337.65625. Some returned heights lay outside the query segment.
Two causes were isolated: signed32-bit plane dot products/subtraction overflow,
and the existing12-bit ratio rounding a distant intersection back to the
segment's start. A64-bit product alone did not fix the precision case.

`tools/prepare_integrated_collision.py` modifies only the generated copy of the
pinned port raycast. Plane/edge products use64bits; short safe segments keep
the original Q12-to-Q16 interpolation. Overflow cases and segments whose
per-axis ratio quantum would exceed0.5world units use the wider ratio path in
`raycast_arithmetic.hpp`, with explicit downward rounding. The length threshold
is2048world units because the legacy ratio has12fractional bits and KCL uses
64coordinate units per world unit. No vendored/pinned source is edited.

The saved-capture replay supports `--safe-rays`: it redirects only the offline
CPU copy to the separately compiled adaptation. With the original immutable
baseline report supplied, the two water triangles, six transform cases and
20short ceiling results compare equal. All20long ceiling intersections now
match independently reconstructed prism-plane tolerances, maximum error
0.0443044355world units. This establishes the tested ray arithmetic, not the
N64 ceiling admission contract or complete movement integration. Both the
unmodified baseline and corrected run remain under private build output.

`tools/test_integrated_raycast.py` is now a mandatory integrated-build gate:
63ordinary interpolation cases are unchanged,81wide cases match the independent
long-double floor oracle, ASan/UBSan pass and the test compiles for ARM. The
complete candidatec8126d8431f00a80 builds/audits, with the ray adaptation and
gate frozen in its246-file source-evidence manifest. ELF SHA256:
`c8126d8431f00a80f3fd9824be7ff01b39e7a6fa9ae999c905ad2b6203c78e9e`;
3DSX SHA256:
`c7e9f0afd6bfad1e6114074ef50188b9dc1b1515de29c1e7b60d83ba50034f9e`.
No emulator launch, physical result, final CIA or final QR is implied by this
new build. Wall/ceiling adapters, production physics handoff and the rest of
the acceptance checklist remain open.

## Ceiling admission bridge and N64 entry, 2026-10-18

`sm64ds_contact_plane_height` refines a ray contact using the registered
collider's original virtual GetTriangleOrigin, its world normal, and the
query's horizontal position. Registry slot, collider address, owner address
and unique ID are checked before dispatch. Triangle IDs are constrained to
the original signed16-bit range before the virtual call.

`sm64ds_motion_ceiling` accepts integer N64 world coordinates, searches from
Y-78through the100000sentinel, refines the contacted plane, and applies the
original ceiling normal/admission tests. It includes an exact admission
boundary despite the original line routine's strict front/back crossing
test, handles a rejected near-boundary plane with forward progress, and
reports repeated/stale contacts as errors. Miss/invalid calls preserve output;
coordinates and midpoint/segment ranges are checked before narrowing.

The ARM saved-capture replay now checks60boundary cases over20castle ceiling
faces against independently computed rational prism-plane heights, plus six
empty/sentinel/invalid-coordinate/pointer cases. All pass. The20long-ray,
short-ray/water/transform regression checks also remain passing. Latest replay
contains156calls; this is not full-game moving-ceiling or physical acceptance.

The generated N64 core now has an optional ceiling provider at find_ceil.
No registered provider leaves the original mesh behavior intact; registered
misses/errors return the100000/no-surface result without falling through to
a different geometry owner. Source/header copies and current host/ARM archives
are checked by the contact audit. Actual N64 find_ceil tests on a real loaded
castle ceiling confirm provider hit, explicit miss despite an existing mesh
ceiling, and restoration of the original height/pointer after unregistering.
The360tick cache/contact test remains passing. These entry and backend pieces
still require the production DS-world callback/surface-cache connection; walls,
CLPS semantics, platform lifetimes and full actor integration remain open.

## Unified world-session ownership, 2026-10-18

`world_session.hpp` joins the optional collision-provider entries, bounded
stable cache and actual solver-contact enumeration. It exclusively owns the
headless engine while open, requires all floor/ceiling/wall/property callbacks,
pins every retained contact before creation/ticks, distinguishes original
collider/actor/owner/KCL/triangle/slot identity, and refuses unknown properties
instead of silently assigning ordinary ground. Platform transforms require
an explicit retaining owner before they can remain active. New geometry and
material callbacks are a required interface; the real DS wall/material backend
is not supplied by the synthetic test fixture.

The session publishes staged MarioState output only after valid provider and
solver results. Geometry errors latch the session until it is closed. Destruction
deletes owned solver instances, unregisters callbacks, terminates the exclusive
headless core, then resets cached storage. Reopening and failed creation are
tested. It must live in resident/static storage or an owner allocation outside
simulation, not on the3DS main stack. Current imported spawn records are Vec3s;
out-of-range/nonfinite spawns explicitly reject instead of wrapping that ABI.

The new generated N64 wall entry stages callback results before committing
position corrections. It validates counts, finite coordinates, unchangedY,
radius/offset, valid wall normals and surface pointers. Invalid/missing results
never use a different mesh; malformed callback results retain input coordinates.
This entry does not substitute DS sphere correction for N64 wall behavior.

The extended contact gate now executes840WorldSession solver ticks in an
explicit synthetic floor/wall/ceiling world: two actors, a wall stop atX100,
normal jump peakY512.5and low-roof peakY285.5. An additional120ticks use the
actual imported raw-CirclePad adjustment through motion64_tick_circle;Xagain
stops at100. Those120are included in840, not extra. Faulted geometry leaves
caller output unchanged, failed creation cleans up, and all actors disappear
before cache destruction. Unit wall tests exercise NaN, forbiddenYmovement,
error/missing results and invalid contact counts under ASan/UBSan. Host/ARM
core builds, ARM session compilation and modified-headless/ownership sanitizer
tests pass; the earlier360contact-churn tick fixture is separate. See
`build/movement-adapter/solver-contacts-gate-latest.json`.

This is integrated headless session behavior in a synthetic world, not actual
DS-level movement or physical performance. Real DS wall candidate/narrow-phase
queries, explicit CLPS mapping, platform lifetime/velocity ownership and the
main Player simulation/presentation handoff remain required. No production
Control selector is enabled by this component work.

## CLPS locomotion classes and stretched geometry, 2026-10-18

`material_profile.hpp` decodes both original CLPS words, retaining the sound
index, low-byte parameter, both auxiliary bitfields, collision filter flags
and class1latch metadata. Classes0/1/2/3/4map to default/not-slippery/
not-slippery/slippery/very-slippery locomotion. Class5also setsTERRAIN_SLIDE:
the original DS Player tables show that this is N64's one-degree slide-terrain
special case promoted to a per-triangle class. It must not become ordinary
very-slippery ground without the slide terrain rule.

`audit_material_profile.py` compiles/runs the original ARM bitfield accessors
against the host decoder(1850field comparisons), and the host sanitizer test
compares all six classes and4097Fix12normal-Yvalues against both the original
DS slope function and the actual imported N64 mario_floor_is_slope function:
24582cases pass. The two-word/sound/parameter metadata remains intact. Original
physical-capture identity/text is checked before reading its23CLPS entries;
all decode as22ordinary solids and one water material. Receipt:
`build/movement-adapter/material-profile-gate-latest.json`.

The locomotion mapper intentionally does not assign ordinary floor behavior to
water, toxic/nonzero DS effects or unknown friction classes. They are returned
as explicit distinct dispositions, without modifying the supplied N64 surface
on rejected mapping. Full hazard/quicksand/death/warp/gameplay-state delegation
is still required; this gate does not establish that those effects work in the
new production modes. The original DS mode remains its existing owner.

While checking native geometry, a contact-plane defect was found for
dBgW_KcMbgSclY: it inherits GetTriangleOrigin without its additionalYscale,
while the actual collision queries call func_0203aa10 to apply that scale
before the world matrix. Plane refinement now recognizes that vtable, bounds
the KCL triangle/anchor reads and uses the original collision transform.
The saved-capture CPU replay creates a controlled offline SclY clone from the
saved water collider, then verifies exact anchor-plane heights at0.5/1/2scale.
All three pass; ordinary moving/water/short/long-ceiling/boundary cases remain
passing(162total replay calls). The clone exists only in Unicorn memory and
neither the user's dump nor a live game is modified. This is not evidence of
playing a stretched moving platform in a level. No new full-game candidate
follows from these still-unlinked adapter components.


### Live prism extraction and world geometry — 2026-10-18

`sm64ds_contact_prism` now exports a bounded112-byte snapshot of a registered
DS KCL prism: raw fractional anchor/extent/normals, attribute, KCL identity,
current world matrix, extra Y scale and original collision-world anchor. It
accepts only the three known KCL vtables, rechecks slot/collider/actor/owner
identity and current virtual surface properties, and bounds position, normal,
triangle and CLPS indices before lookup. It leaves output unchanged on failure;
no allocation or file I/O occurs. This assumes live game-owned allocations;
array/identity checks are not a general arbitrary-pointer memory sandbox.

Saved physical RAM replay passes all1245available triangles,26stale/malformed
rejections, six controlled translated/yaw/pitch transforms and three cloned
scaled-Y cases. Controlled mutations exist only in offline Unicorn memory.
The user's original incomplete dump and matching ELF remain unchanged.

`world_triangle.hpp` converts those snapshots to fractional world vertices
while retaining the original query normal and rounded collision anchor
separately. It does not silently replace either with a recomputed plane.
All1254captured/controlled records pass scalar transform/winding/normal checks
and rejected-output invariants under ASan/UBSan; ARM syntax compilation also
passes. The replay retains previous short-ray baseline equality and long-ray/
ceiling checks(2696total ARM calls). Receipt:
`build/movement-adapter/capture-ground-1792329753047674000/report.json`.

These components are still unlinked from production movement. DS wall-cylinder
broad/narrow phase, dynamic-platform ownership, gameplay effects delegation and
main-game controller integration remain required. This is geometry evidence,
not gameplay/FPS acceptance, and does not change candidatec8126d8431f00a80.


### Fractional N64 wall narrow phase — 2026-10-18

`wall_contact.hpp` implements N64 wall-cylinder admission and lateral push
using fractional DS world vertices and the original DS plane normal/anchor.
It retains both projection branches and normal signs, the200radius ceiling,
vertical wall classification, inclusive triangle edges, same-input-center
multi-wall correction and unchanged output on miss/invalid input. It does not
substitute a DS sphere correction for the N64 cylinder. Candidate enumeration,
DS actor/pass-through policy and stable surface ownership remain separate.

`audit_wall_contact.py` compares37800queries against the actual imported N64
mesh entry with no registered provider:3774hits agree in admission and push.
The test covers ten orientations, three tilts, fractional centers, boundary
heights and five radii. A six-hit corner proves accumulation against the
original center and first-four retained-wall ordering; dedicated fractional
edges and malformed inputs pass. Test code, the original collision TU and
surface loader are ASan/UBSan instrumented; other imported archive members are
not. ARM syntax compilation passes. Receipt:
`build/movement-adapter/wall-contact-1792330044766222000/report.json`.

The WorldSession gate now calls this same narrow phase rather than its old
single-plane push formula. All840actual headless solver ticks still pass,
including wall stop atX100, jump/low roof,120raw CirclePad ticks and staged
fault/teardown checks. Receipt:
`build/movement-adapter/solver-contacts-1792330045876795000/report.json`.
These are CPU fixtures; no new emulator run, production physics mode, final
CIA/QR or physical performance acceptance is implied.


### Original registry filters and preloaded wall geometry — 2026-10-18

Added production-side geometry enumeration (`sm64ds_world_prism_count/at`),
sharing the contact reader's bounds/identity checks. All1245saved triangles
match the original virtual-surface fixture; empty slots and invalid indices
are checked. The pass-through bridge calls original actor exclusion and CLPS
rules using an explicit caller-supplied detect mask. Walls must also pass the
original wall discriminator: effect0x14 tests detect bit0x40 only for a wall.
This distinction was identified in the original sphere contact source and is
now explicit in the API.12288controlled flag/effect/mask cases plus one saved
actor-exclusion case pass original ARM execution. Ability-specific masks still
belong to the real gameplay owner and are not yet wired.

`ds_world_walls.cpp` supplies an uncached complete reference: original registry
order/filters plus fractional N64 cylinder tests and first-four contact return.
Ten saved-registry queries agree between ARM replay and host reference, nine
hits and one distant miss; failure/miss outputs are staged. These are collision
queries in one saved scene, not ten gameplay performance scenarios.

`WorldWalls<1024>` preloads static wall geometry and retains all moving
triangles, including current floors which may later rotate into walls. It uses
resident bounded storage, no per-query allocation or SD access, and explicit
scene rebuild ownership. It detects changed registry/count/KCL/owner identities
before returning cached contacts; the DS filter revalidates actual hits. Same-
address static geometry mutation still requires the scene owner's explicit
invalidation, which remains part of production integration.

The saved scene retains451entries(449walls+2moving water triangles), with a
181392-byte ARM owner. Cached results are byte-identical to the uncached ARM
path in all ten queries. Geometry enumeration/refresh reads drop from1245per
reference query to4per cached query; this count excludes the unchanged original
contact revalidation for actual hits and does not measure total CPU time/FPS.
A controlled original Transform rotates saved water geometry by90degrees into
a wall: cached queries refresh it and match the reference, returning the real
moving collider when water detection is enabled. Host ASan/UBSan checks cover
cache capacity failure, stale ownership, late query failure and unchanged
outputs. Receipt:
`build/movement-adapter/capture-ground-1792330718287286000/report.json`.

The adapter remains unlinked from main-game movement. It still needs stable
material/platform descriptions, scene invalidation hooks, real gameplay mode
ownership and final runtime/hardware acceptance; no new CIA/QR follows yet.


### Material/plane descriptors and original platform transport — 2026-10-18

`surface_description.hpp` combines a fresh validated prism/contact pair into a
SurfaceKey, preserved fractional triangle/material metadata and N64 surface
value. Normal and plane anchor remain the original DS query values; CLPS
parameter does not become an N64 room. Integer compatibility vertices are not
fed back to legacy mesh collision. Water, DS effects, unknown friction and
missing moving-platform ownership return explicit dispositions without writing
partial output. Moving geometry requires a stable platform handle, not a null
transform silently pretending to be static.

All1243ordinary captured contacts produce descriptions; both captured water
contacts remain excluded. Host ASan/UBSan and ARM compilation pass, including
raw metadata/normal/plane/bounds preservation, controlled moving-handle
requirements, seven DS-effect exclusions and invalid-output staging. Receipt:
`build/movement-adapter/surface-description-1792331104788841000/report.json`.

Added `sm64ds_platform_displace`: verify current registry slot, collider, actor,
owner ID, KCL and exact moving subtype, then call the original virtual
TransformPos and GetAngularVelY. Six controlled transforms of the saved moving
collider agree byte-for-byte with direct original virtual calls; independent
matrix/yaw checks stay within0.000190world units. Twelve stale/invalid platform
references leave output unchanged. This avoids requiring an approximate
DS-matrix-to-N64-Euler conversion, but still needs a core carry-provider hook
and stable platform owner before production activation. These controlled
saved-water-mesh cases are not physical gameplay on a platform. Receipt:
`build/movement-adapter/capture-ground-1792331048346259000/report.json`.

Retained-contact audit found a missing lifetime edge: floor/ceiling/wall/
object-floor records can retain transform pointers even when marioObj->platform
is null. SolverContactPins now pins/deduplicates each nested surface transform
as well as the direct platform pointer. Actual-instance tests cover four
independent nested handles with no direct platform, five total handles when
the direct platform is added, and deduplication when they coincide. Existing
360contact-churn and840WorldSession solver ticks continue passing. Receipt:
`build/movement-adapter/solver-contacts-1792331105485601000/report.json`.
Main-game physics, DS hazards/progression delegation, carry integration and
physical/60FPS acceptance remain open. No candidate/CIA/QR changed here.


### Core platform provider, stable handles and unified native backend — 2026-10-18

The generated N64 platform-displacement TU now checks an optional DS carry
provider before legacy Euler displacement. A registered valid result commits
position/yaw exactly once; malformed/error results preserve position/yaw, count
a fault and return without falling through to legacy movement. Unregistered
behavior remains original. The pinned vendor source is untouched. Host and ARM
core archives build with identical frozen source manifests.

The actual core platform function is tested for default legacy translation,
registered position/yaw transport, rejected staged output and unregistration
restoring legacy translation. Provider unit tests reject nonfinite data,
invalid callback statuses and null arguments. WorldSession registers/tears down
the carry provider, includes its faults in tick/create acceptance and refuses a
moving descriptor without both retention and displacement owners.

`StablePlatforms` owns bounded stable identity handles backed by DS collider/
actor/owner/KCL/subtype references. It protects retained and current-tick
handles, rejects foreign pins and capacity exhaustion, and dispatches transport
through the previously validated original DS TransformPos bridge. Position
conversion uses bounded Fix12 and failed transport leaves output unchanged.
Controlled tests cover replacement owner IDs without reusing pinned storage,
stale/foreign references and failed/native-yaw output staging.32handles cost
2320bytes on ARM; no simulation allocation or file I/O occurs.

Added `DsWorldSource`, joining original ground/ceiling queries, preloaded walls,
CLPS/plane descriptions and stable platform transport as WorldSession callbacks.
Scene prepare/reset is forbidden while any session/solver remains active.
The1024wall/32platform backend compiles fully for host and ARM and occupies
183720resident ARM bytes. It still needs end-to-end saved-RAM execution and
production scene/ability/animation/audio/state-transition integration. Its
ordinary-material path rejects DS hazards instead of flattening them, so hazard
ownership must be completed before enabling general gameplay modes.

The session gate passes960actual solver steps, including120platform ticks,
with a deliberately large legacy velocity proving the DS callback owns the
transport. Provider failure preserves caller output and missing carry ownership
rejects creation. Modified headless/platform-displacement TUs and test/ownership
code are ASan/UBSan instrumented; other imported archive members are not.
Receipt: `build/movement-adapter/solver-contacts-gate-latest.json` (immutable run
path recorded in itsdirectoryfield). No emulator, main-game candidate or
physical FPS acceptance follows from these CPU/compile gates.


### Unified saved-world session and per-tick wall refresh — 2026-10-18

`audit_saved_world_session.py` now runs the actual host N64 WorldSession and
DsWorldSource together. A test-only fixed-width-record bridge forwards DS
queries to a fresh Unicorn copy of the verified physical dump and ARM overlay;
there is no converted N64 mesh fallback. The user's original dump is preserved.
The overlay is ELF-hash checked, source evidence is verified and only wholly
captured readable mappings are loaded. The incomplete-prefix limitation stays
explicit; no claim of whole-process or physical runtime coverage follows.

180idle/walk/jump steps and240directed-wall steps pass(420total). Eight saved
castle wall positions produce612original pass-through/filter calls across the
directed runs. One additional directed spawn has no admitted DS floor and is
recorded as not executed, rather than counted as a successful movement case.
There are no bridge/provider errors. Scene-source reset is refused while a
session is active; actor deletion and final teardown succeed. This is one frozen
world, not the final ten-scenario/three-dump gameplay campaign. The host session
bridge is unsanitized; separate component/ownership sanitizer gates retain
their stated scope. Immutable pre-optimization receipt:
`build/movement-adapter/saved-world-session-1792332129564594000/report.json`.

Counters exposed redundant registry and moving-geometry refresh on every N64
wall subquery. WorldWalls now offers an explicit begin_tick/query_prepared
boundary, and DsWorldSource refreshes once from the session's pre-tick retention
callback. This requires simulation-thread ownership with no actor/scene/collider
mutations interleaved during the step; such a boundary requires invalidation
and a new snapshot. Standalone query() still refreshes immediately. Actual hits
continue original DS validation, and stale/unprepared/invalidated cache queries
leave output unchanged. Eight subqueries share one refresh in the sanitizer
contract test; ARM compilation and previous saved moving-floor-to-wall cases
continue passing.

The same420saved-world steps retain exactly identical sampled positions,
velocities/actions and all directed-case results after this optimization.
Registry count calls fall72336to10320(85.73%); prism enumeration/refresh calls
fall13297to2961(77.73%). Ground2168, ceiling1483, contact-prism4254and filter612
counts remain unchanged. These are actual bridge-call work counts, not elapsed
3DS CPU time or FPS. Receipt:
`build/movement-adapter/saved-world-session-1792332324797190000/report.json`.
No main-game candidate/CIA/QR or physical60FPS acceptance changed. Next required
integration includes the lab/main player owner, scene rebuilds and DS gameplay
ability/hazard/audio/animation transitions.


### Native DS-world player integration and Azahar presentation — 2026-10-18

The separate player lab now uses DsWorldSource/WorldSession instead of the
rounded converted castle mesh. Scene preparation, retained surfaces and DS
platform handles live in resident owners. Original mode, DS ability delegation,
missing players, disabled control and scene exits release the N64 session;
re-entry creates it from the actual DS position/state. Only a successful native
tick commits displacement. A native error leaves DS state uncommitted, logs the
fault and visibly selects Original; scripted acceptance stops on that fault.
This is an explicit experimental fault path, not a claim of seamless final
physics integration. Wario's DS table ratios now apply in both native modes,
consistent with the stated character policy; held-jump delegation remains
Hybrid-only for Luigi/Yoshi. Broader handoff behavior still needs play tests.

The builder writes a fresh immutable lab directory, links the original-world
query bridge and freezes lab/adapter sources with image/core hashes. It does
not overwrite earlier lab outputs or alter the main Controller menu. Candidate:
`build/movement-lab/native-world-1792332783467301000`.
ELF75f09493eee09bc4b08010e9468ee1060ef1720ac655d9bda5e5b754a41cf7b8;
3DSXa2bb48ca089389ba7385f6215c5e84bb9ce26973d2ac735f5c4f38c93a5c916e.
The integrated main ELF remainsc8126d8431f00a80.

Actual isolated New-model Azahar run:
`build/movement-lab/attempt-1792332884628734000`.
It completed the960-step presentation script with no emulated/native-adapter
fault;1016native ticks were logged before the bounded supervisor stopped it.
All four characters pass original DS animation cursor/rate/loop and foot-contact
checks(two walking cycles and six running cycles each minimum), and original
DS voice API IDs0/1are observed for each. Submitted stereo PCM is nonzero:
5754880bytes,43.959seconds, peak26100. This validates dispatch/submission, not
subjective audible fidelity. The optional PCM tap is active only for the
scripted diagnostics. Receipt: `native-world-summary.json`, `result.json` and
`presentation-audit.json` in that attempt.

During observation an empty source-app window was opened by selecting the
source bundle rather than the owned attempt's copied app. It was closed; the
replay process continued to completion. No visual screenshot is claimed from
that selection. Future UI inspection must select the live copied bundle from
its attempt/process record and must not reopen a stopped test app.

This remains one castle laboratory replay: Original/Hybrid interactive behavior,
Yoshi progression/egg, transformations, Wario interactions, scene changes and
physical Old/New performance remain unaccepted. The old converted-mesh host
trajectory oracle is not a valid comparison for this native-world candidate.
No final CIA/QR or production physics-mode completion is implied.


### Interactive Old-model handoffs and button-history fix — 2026-10-18

Added opt-in SM64DS_LAB_TRACE observation without forcing scripted input or the
8MiB PCM capture buffer. The laboratory runner accepts an explicit model/trace
and bounded duration, and disables update-on-start only in its private Qt test
profile so an updater modal cannot intercept game inputs. The copied bundle
from the live attempt record is used for UI inspection.

Real UI/HID input on Old-model Azahar verified Original CirclePad movement,
Hybrid Luigi/Yoshi held-jump delegation and native return, Yoshi's empty-target
DS tongue action from N64 and return, and Wario's original table ratios in N64.
Initial run: `build/movement-lab/attempt-1792333375337050000`.
It found a real ownership-transfer defect: recreating the N64 instance resets
button history, so continuing to hold jump through a DS landing created a new
A-press and an unwanted repeat jump. This run is recorded as defect discovery,
not an all-pass movement result. An updater prompt initially intercepted some
input; those observations are not treated as movement evidence.

`motion64_seed_buttons` now writes the actual instance Controller's previous
A/B/Z held bits and clears their pressed bits without changing global binding
or actions. WorldSession checks the actor ID; the player handoff seeds
`held & ~down`, preserving a genuinely new press while suppressing a continued
hold. The direct native core regression covers60grounded held updates, release
and a new valid jump(62steps); session coverage is1022steps. Host and ARM
archives rebuild, invalid/deleted IDs and binding preservation are checked, and
ASan/UBSan scope remains the instrumented headless/platform/test/ownership code.
Receipt: `build/movement-adapter/solver-contacts-1792334104698056000/report.json`.

Corrected immutable laboratory:
`build/movement-lab/native-world-1792334143509034000`;
3DSX266dfd559e4b0b89e1ffaabd5653656cd7c1342d2a4935adf1b80decc50f7812.
Follow-up Old-model UI run:
`build/movement-lab/attempt-1792334174374957000`.
The actual log proves Luigi resumes native control grounded while B is still
held:23native updates keep the same ground height before release, and a later
new press producesY296/vertical velocity38. Hybrid Yoshi delegation and N64's
empty-target DS tongue action also return to native control. No native/emulated
fault is recorded. `audit_handoffs.py` checks these observations and writes
`handoff-audit.json`. Swallowing an actor, making an egg, block interactions,
transformations and progression remain untested.

The new core still passes the420-step original-ARM saved-world movement/wall
replay with every recorded trajectory/action and query count unchanged.
Receipt: `build/movement-adapter/saved-world-session-1792334404634241000/` with
`button-seed-regression.json` comparing the distinct before/after core hashes.
No production Controller/CIA/QR or physical FPS acceptance follows here.

## Old-model live RAM capture acceptance (2026-10-18)

Build `native-world-1792335046869735000`, image
`c8782c11f0e82a2d10d851435f2657a048d37db1aa1c138ba3a58e36d6fd169c`,
ELF `fc0015236f66a1bcc9191be99c3f6ebab14d482e76b8b460a8681d31192a83a8`.
The runner supports opt-in `--dumps`: private settings enable the actual main
capture and profile-local Shift+A emits raw L+R+A. The lab strips that chord
from held/pressed movement inputs so capture does not crouch/attack afterward.
Normal runs preserve their usual shoulder bindings and diagnostics setting.

Attempt `build/movement-lab/attempt-1792335062852235000` was stopped explicitly
after117.756seconds, with no logged fault. Two real captures,114336816bytes
each, showed the progress screen and then restored responsive movement.
Screenshots were observed through CUA, not saved as standalone image artifacts.

`tools/movement_lab/audit_ram.py` validates the closed owned attempt, immutable
image/source receipt, complete readable mappings and anchors,2901952bytes of
exact ELF text, native ownership and matching DS/N64 coordinates. Mario N64
and Luigi Hybrid both pass. Allocator lists pass; allocated malloc chunks are
38514152 and38936408bytes, free linear14073600 and13499904bytes, free VRAM
778240bytes in both samples. Character/cache changes make this no leak test.

Audit command (before retirement):
```
.venv/integrated-native/bin/python tools/movement_lab/audit_ram.py build/movement-lab/attempt-1792335062852235000 --retire-memory
```
After successful analysis only the two raw VM files were retired, with identities
in `ram-retirement.json`. Reports, anchors, traces, render data and the user's
original `dumps/` remain. This is not the final ten-scenario campaign, complete
physical RAM, physical crash regression or console FPS acceptance.

## Retained lower panel (2026-10-18)

The lab no longer repaints/publishes its entire lower image each input tick.
`render()` returns a new image only when mode, character or status changes;
publication follows successful paint. Existing acknowledged channel images
remain available for every presentation, including after real dump progress.

Buildnative-world-1792335807525616000 image
`d24d3960357f497eabf31ddb8f068dd3d2dd7a85489b8daaed1dea74f27b9a96`,
ELF `ff51f77b8d1a35e70dfc8cbf5bbfd5fec64a1946374f1d80903eb0775e373a83`.
Old attempt1792335829788294000: two complete RAM captures validate exact
2902192-byte code, native actor ownership/positions and allocator lists.
Actual UI changes Original→N64→Hybrid and Mario→Luigi, held jump and dump
progress/resume were inspected. No logged fault; explicit stop148.737seconds.
`panel-comparison.json` records before/after scene3 timing windows and limits.
Only these analyzed rawVM files were retired by the strict lab audit; detailed
reports/trace and originals retained. Main physics integration remains open.
