# Typed original display producers

The native game now captures original brightness, blend and background producer
semantics without sending their old output through the compatibility renderer.
**These states are not yet a complete native display consumer.** Original
background assets, fonts, masked effects, transparency and pixel policies still
need rendering integration. The complete-scene gate remains closed for the title.

## Ownership and publication

`menu_display_state.hpp` owns two effect states and eight bounded background
records. They contain logical source identities, dimensions, priorities, scroll
positions and dirty/content flags. They never retain a source pointer, tilemap,
register packet, GPU handle or VRAM address. A diagnostic export labels their
scope `typed-source-display-state`; it cannot claim a full scene, physical LCD
photograph or RAM/VRAM dump.

Effects persist until a source setter changes them. Background records latch at
`020c03b0`, separately for each engine, with publication epochs. This happens
between game updates in the observed title. Clearing them at game-update begin
would erase the active display state before capture. Each original publication
clears only that engine's records, then the original selector chooses its nodes.
Duplicate or unsupported submissions mark coverage failed, without publishing a
valid prefix as complete. A scene change resets the owned state.

Native source seams:

| Producer | Native behavior |
| --- | --- |
| `G2x::SetBlendBrightness` | Capture target-layer selection and signed strength; skip original register writes. |
| `020551f0` | Change brightness strength while preserving disabled/alpha modes; skip original writes. |
| `G2x::SetBlendAlpha` | Capture both target sets and both coefficients; skip original writes. |
| `020c897c` | Preserve the three exact source animation-state writes; capture disable, refuse unsupported mosaic. |
| `020c076c` | Capture selected background; preserve dirty-flag clearing; skip cache flush, tilemap transfer and scroll writes. |
| `020c03b0` | Open that engine's publication epoch, then retain original CPU node selection. Its remaining priority-register writes are not migrated yet. |

The maintained comparison mode forwards original producers. Brightness inputs
through ±31 and alpha coefficients through 31 saturate at the original effective
limit of 16. Inputs outside the established source contract remain unsupported.
Repeated identical effect setters do not advance semantic generation. Normal
capture allocates nothing, converts no pixels and performs no file IO.

`whole_tone` permits a whole-output lowering only when every potentially visible
layer and backdrop is covered, or the effect has no influence. Partial masks
and meaningful alpha blending remain explicit. It is a coverage rule, not proof
that the compositor's continuous colour calculation reproduces every original
six-bit effect. No such lowering is used to admit the original title yet.

`unaffected` is a conservative current gate: unknown state, active fades/blend
and unprepared background submissions cannot silently pass because other DS
layers happen to be disabled. It must be replaced by concrete native coverage
as those consumers are implemented, not removed to allow a partial frame.

## Actual title evidence

At component frame 121, Old/New isolated runs produce identical source snapshots:

| Engine | LCD in reviewed title mode | Effect |
| --- | --- | --- |
| A | Lower | All six layer roles selected, brightness 0. |
| B | Upper | Backgrounds and backdrop selected, brightness −16; OBJ excluded. |

The five selected background records are:

| Engine | Logical resource | BG | Priority | Tile dimensions | Scroll |
| --- | ---: | ---: | ---: | --- | --- |
| A | 6 | 1 | 1 | 32×24 | 0,0 |
| A | 1 | 3 | 1 | 32×32 | 0,0 |
| B | 7 | 1 | 0 | 32×24 | 0,0 |
| B | 3 | 2 | 3 | 32×32 | 0,0 |
| B | 2 | 3 | 3 | 32×32 | 0,0 |

Selection is not the same as display-enable coverage: the B/BG1 descriptor is
submitted even when that layer is disabled. Native admission still needs the
original controller's typed enable/window/backdrop semantics. The initial
configuration's priorities are also overridden by these live submissions;
using the initialization values alone would produce incorrect ordering.

At frame 121, each profile has 600 background callbacks, five dirty publications
and 9216 avoided tilemap transfer bytes. **This is accumulated startup work, not
9216 bytes/frame**: the original already retained unchanged tilemaps. Repeated
scroll/priority work still requires measurement before further optimization.
Opt-in profiling measures the two source background publication groups, including
selection and nested native capture. Default runs read no profiling clock.
The sampled Old guest accumulated 363140 ticks over 120 source cycles, about
0.0113 ms/cycle; this excludes glyph construction, other producers and rendering,
and is not a physical-hardware benchmark. Identified static symbol payload is
208 bytes of owned state, 72 bytes of counters and a 4-byte optional clock
pointer, excluding alignment and code. The existing 925464-byte runtime
workspace and GPU payload counters are unchanged by this increment.

Offline source inspection identifies static NCSC resources 1–4 and null-source
runtime objects 0/5/6/7. The 6/7 records above are mutable font surfaces, so a
static whole-screen image cannot replace their source text behavior. Preparing
sparse native tile geometry/atlases and capturing original glyph/text semantics
is the next rendering work. Original source containers are read only offline.

## Validation and limits

The portable host suite checks masked-tone refusal, persistence, independent
engine epochs, duplicate rejection, saturation, state ownership and recovery.
The ROM-free ARM differential exercises 4096 authored brightness/strength/alpha/
reset sequences and 2048 background cases against the pinned original functions.
It checks source animation/dirty bytes, signed scroll, inactive forwarding and
opt-in clock behavior. The migrated helper intervals write zero graphics MMIO;
this does not claim every remaining original controller write is gone.

Actual Old/New probes complete 180 updates each, with no observed producer
failures or unmapped accesses and no complete original native frame accepted.
The native 3D/caption packet stays byte-identical to the earlier checkpoint.
The same candidate with native mode disabled preserves both original reference
LCD captures byte for byte. No GPU backend/shader changed in this increment;
the prior GPU component evidence is retained, without claiming new fade visuals
or physical performance. All private captures, binaries and reports stay under
ignored `build/native-renderer/display-state-r1/`.
