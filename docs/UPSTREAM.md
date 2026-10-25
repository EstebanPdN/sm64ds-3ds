# Source provenance

Dependencies are pinned independently; one reference does not silently
replace another. Keep upstream notices and compare actual files before
updating a pin.

| Component | Source of truth | Use |
| --- | --- | --- |
| Native SM64DS backend | `reference-native.lock.json`: Sébastien Breton `88725627c529e5d9db57abc5544513d8741fa07d` | 768 vendored files under `third_party/sm64ds-3ds/`. |
| Backend decompilation | Same lock: `d3022c1463a260ba67a3dc148aea01eb8de92ca3` | Public source checkout prepared locally; extracted data stays private. |
| libsm64 | `.gitmodules` and gitlink `fd11813208272b4271d92bd92feb8f3fdbe61be5` | Public motion-library source, cloned recursively. |
| SM64 3DS movement reference | `tools/movement_lab/port_source.py`: `3f3c01eae67d6e9f3af89bc8073e37a5d153e00f` | Checked function import into a generated solver copy. |
| Historical game/decoder references | `upstream.lock` | Earlier runtime/viewer experiments, retained separately. |

Repositories:
[native backend](https://github.com/SebastienBreton/sm64ds-3ds),
[backend decompilation](https://github.com/SebastienBreton/sm64ds-decomp),
[Tango decompilation](https://github.com/tangosdev/sm64ds-decomp),
[libsm64](https://github.com/libsm64/libsm64),
[SM64 port](https://github.com/mkst/sm64-port).

## Why vendor one dependency and submodule another?

The backend snapshot already has per-file hashes used by build gates and
source transformations. Tracking that same snapshot preserves the exact
contract. libsm64 is an intact public Git checkout, so a pinned submodule
preserves its complete origin/history without copying its repository
metadata or pretending it is project-owned source.

The backend's nested decompilation is prepared locally by the builder or
`tools/setup_dependencies.py`. Its extracted directories, generated build
outputs and ROM resources are never tracked. The movement reference is
also prepared under ignored build output or selected by an explicit path;
its animation definitions remain local game-derived inputs.

## Updating a dependency

Change one pin deliberately. Inspect upstream differences and licenses,
revalidate preparation anchors, run affected synthetic/ARM/resource gates,
and build a new candidate. Do not overwrite the pinned provider to make a
contract mismatch disappear. Keep modified derivatives under `build/` and
retain the original hashes in their receipts.

`python3 scripts/check-repository.py` verifies the vendored bytes and
gitlink. `python3 tools/setup_dependencies.py --check` additionally verifies
the public reference checkouts without updating or resetting them.

See [third-party notices](legal/THIRD_PARTY_NOTICES.md) for terms and
[asset context](../assets/README.md) for interface and packaging resources.
