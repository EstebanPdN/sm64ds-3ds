# OV007 native menu arithmetic

`native_menu_math.cpp` replaces four arithmetic leaves from upstream pin
`6230b2996636e3fd0e03cb178161c94cb4b73cc9`:

| Symbol | Original role | Native adaptation |
| --- | --- | --- |
| `func_ov007_020c30ac` | Closest point in a 3D array | Wrapped 32-bit subtraction, wrapped 64-bit sum, signed comparison and the original rounded square root |
| `func_ov007_020c31e0` | Closest point in separate 2D arrays | Same integer search semantics, including empty arrays and untouched distance output after rejection |
| `func_ov007_020c368c` | Normalize an array of `Vector3_16` normals | Preserve the 32-bit squared-length intermediate, signed 64-bit division, wrapped products, rounding and optional positive clamp |
| `func_ov007_020c7d60` | Menu particle spring and friction | Preserve the original signed length rounding and use the existing exact `normalize3` primitive; retain original pure-memory force/dot helpers |

`func_ov007_020c368c` is reached by `020cb3dc` and `020cb7c0` while building
normals used by the original menu renderer. `020c30ac` is used by its geometry
searches, `020c31e0` by the menu's point selection, and `020c7d60` by its spring
simulation through `020c7560`. `func_ov007_020c3054` computes only extrema; it
needs no hardware adaptation and remains original.

The replacements compute values directly. They contain no DS register
addresses, arithmetic-unit busy polling, interrupt-mask writes or register
mirrors. The former divider/sqrt pipeline has independent consecutive inputs
in this single simulation context, so serial native calculations preserve the
returned and stored values.

The original `020c368c` first accumulates squared s16 components in a 32-bit
MUL/MLA result and sign-extends that wrapped result. Replacing this with the
existing 64-bit `normalize3` squared sum would change extreme inputs. Its clamp
also takes place after narrowing the result to s16 and only limits positive
values to 4095. As in the original leaf, `count <= 1` processes one vector.

The original `020c30ac` leaves its result index uninitialized when `count <= 0`
and returns an incoming callee-saved register. The native leaf diagnoses this
unsupported input through `sm64ds_native_missing`; it does not invent a search
result. All valid nonempty searches are compared against the original bytes.
`020c31e0` has a defined empty-array index of -1, which is preserved.

`source/ctr/game_math.cpp` also provides native `ARMMathSaveState` and
`ARMMathLoadState`. They use the original caller's opaque 28-byte buffer to
save and restore the pending software quotient shared by `fdiv_async`,
`fdiv_result` and `ldiv_result`. The native snapshot has an explicit tag,
version, quotient, complemented quotient and reserved word. Loading an invalid
snapshot fails explicitly. Its contents describe native software state; they
are not presented as a reconstruction of the original DS register dump.

The adopted scalar menu leaves consume their arithmetic results internally.
No caller-visible pending sqrt operation remains. Save/load preserves the
pending native quotient across render callbacks, including nested saves.
Extra SDK threads/interrupt contexts and raw consumers of the old DS snapshot
format remain outside this native single-context contract.

Run the independent CPU test with the existing local Unicorn environment:

```sh
/tmp/sm64ds-primitives-tests/bin/python tools/test_native_menu_math_arm.py
```

The test compiles ARM11 objects, runs each native leaf with the DS I/O address
space unmapped, and compares both its result and its memory writes with the
supplied verified ASMP0 ROM leaf and a separate Python integer reference.
Only the original-leaf CPU models the DS arithmetic registers. The cases
include full-range coordinates, subtraction/accumulator overflow, zero
lengths, positive clamps, tied points, accepted/rejected radii, null distance
outputs, multi-vector arrays, spring forces, signed length division, nested
arithmetic snapshots, corrupted snapshots and invalid empty 3D searches.
Canaries and the ARM callee-saved registers are checked.

The report at `build/native-menu-math/report.json` records comparisons and
source/object/ROM hashes. This validates bounded arithmetic and ABI behavior.
It does not establish that the menu executes as an application, that a game
can start, that Old 3DS hardware is playable, or that any FPS target is met.
