# Repository map

The root keeps the product entry point, build configuration, changelog,
contribution guidance and license scope. The existing CMake/Python source
paths remain stable because the preparation tools and regression runners
consume them directly.

| Location | Responsibility |
| --- | --- |
| `source/integrated_native/` | Current boot, archive, rendering and platform adapters. |
| `source/integrated_native/options/` | Settings, pause/touch UI, camera, profiles, audio/storage helpers and diagnostics. |
| `source/integrated_native/movement/` | Solver ownership and bridge to DS actors, geometry and moving platforms. |
| `source/movement_lab/` | Separate movement laboratory application. |
| `source/core/`, `source/ctr/`, `source/game_port/`, `source/game_data/` | Shared parsers and historical native viewer/runtime work. |
| `source/vendor/ntr/`, `include/ntr/` | Attributed NTR decoding source and interfaces. |
| `include/sm64ds/` | Shared/historical runtime declarations. |
| `third_party/sm64ds-3ds/` | Immutable, byte-pinned native provider snapshot. |
| `third_party/libsm64-motion/` | Public motion-library Git submodule. |
| `tools/` | Stable build/preparation entry points, audits, local acquisition and analysis. |
| `tools/movement_lab/` | Solver preparation and lab builders. |
| `tools/sm64ds_packer/` | Cross-platform desktop companion converter. |
| `cmake/` | Portable checks and integrated target definitions. |
| `tests/` | Regression code, authored fixtures and private-input runners. |
| `packaging/` | 3DS title identity, permissions and memory modes. |
| `assets/` | Approved runtime interface and package artwork. |
| `docs/` | Maintained guides, provenance and dated engineering records. |
| `scripts/`, `.github/` | Source integrity checks, CI and contribution forms. |

The checked [layout manifest](repository-layout.json) lists allowed roots,
maintained documents and the sole submodule pin. Run
`python3 scripts/check-repository.py` after staging changes.

## Placement and ownership

Keep current integrations in their existing feature directories. Add a
subdirectory when it owns a clear responsibility; avoid a folder for every
small helper. Tools retain their filenames where build scripts import them.
Start at [tools/README.md](../tools/README.md) to distinguish current entry
points from historical diagnostics.

The pinned provider is a reference input. Local changes belong in
`source/integrated_native/` and `tools/prepare_integrated_*.py`; transformations
run on generated copies, validate their contracts, and reject drift.
Do not reformat imported sources or regenerate their lock hashes as cleanup.

Current public documentation and new metadata use English. Dated notes and
upstream documents retain their original language. Links to historical files
remain stable; their dates and scope distinguish them from current guides.

## Source and local outputs

`build/`, `dist/`, `dumps/`, `upstream/`, local SDKs, dependency extraction,
game inputs, saves, raw captures and old design proposals are ignored. They
remain on the maintainer's computer and do not belong in source history.
Approved generated UI headers and runtime/package artwork stay tracked with
their producer and credit context. Source archives from GitHub do not include
submodule contents; use the documented recursive clone.

## Git conventions

`main` holds the current source. Use focused branches and short English commit
titles. A cleanup commit is titled `Clean project files`. Do not rewrite
published history to change style. BUILD counters identify local compiled
candidates; tags and GitHub releases remain separate publication decisions.
