# Native renderer tools

Host-side tools for the [native renderer](../../source/native_renderer/README.md):
offline asset preparation, laboratory checks, PPU patch fragments and
private delivery. None of them runs on the console, and none of them stores
game data in the repository: cooked banks, catalogs and captures go under
the ignored `build/` tree and are read from the owner's own ROM.

## Asset preparation (offline, from the local ROM)

| Tool | Prepares |
| --- | --- |
| `author_catalog.py` | The native model catalog and its immutable banks. |
| `cook_model.py`, `bank.py`, `textures.py`, `texture_sequence.py` | Model meshes, materials, textures and texture sequences (SM64NMD2/3 banks). |
| `image_bank.py` | Image-only banks (RGBA8 or A4). |
| `menu2d.py`, `menu_obj.py`, `menu_backdrops.py`, `backgrounds.py`, `nitro2d.py`, `source_members.py` | Menu cells, OBJ cells, menu backgrounds and sparse 2D backgrounds. |
| `font.py`, `particles.py` | The menu font mask and particle images. |
| `*_profiles.json` with `animation_profiles.py`, `sprite_profiles.py` | Reviewed associations (file IDs and offsets only, no game data), checked against the local ROM. |
| `audit_models.py`, `inventory_cooker.py` | Inventory and admission reports before conversion. |

### Prepare the BUILD 142 SD data

Use the source at the [beta-01 tag](https://github.com/EstebanPdN/sm64ds-3ds/tree/beta-01)
and your own European **ASMP revision 0** Nintendo DS dump. It should be
16,777,216 bytes, with SHA-1 `1367529f2cb23e76ef295cb1727333ae8f0a6cd7`.

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install ndspy==4.2.0 Pillow==12.3.0 numpy
python tools/setup_dependencies.py --backend-only
python -m tools.native_renderer.prepare_sd --rom /path/to/europe.nds --output build/beta-01-sd
```

Use Python 3.11 or later and Git. The converter fetches the pinned public
[SM64 movement source](https://github.com/mkst/sm64-port/tree/3f3c01eae67d6e9f3af89bc8073e37a5d153e00f)
on the first run. It uses that source's animation definitions and your DS ROM.
If you already have the pinned checkout, pass `--motion-source /path/to/sm64-port`.

Copy these three outputs into `SD:/3ds/sm64ds/`, preserving their names.

- `prepared.sm64ds` is the game companion.
- `native/` holds the 542 renderer banks.
- `build142-data.bin` holds the linked tables and artwork the executable loads
  at startup. The loader checks its exact size and SHA-256.

Both launch formats read the same files. Keep your saves and settings in place.
Keep the generated files private. When rerunning the converter, choose a new
output directory under `build/`.

## Laboratory and capture checks

`check_*_capture.py`, `check_menu_backdrops.py` and `run_asset_lab.py`
compare a laboratory build's GPU readback with an independent oracle
(integer UV and colour math, the original PPU, or the CPU artwork). They
check one renderer feature in isolation, not a whole game scene.
`component_fixture.py`, `analyze_component_batches.py`,
`component_batch_report.cpp`, `compare_native_timelines.py` and
`symbolize_writers.py` read private component captures, frame timelines and
writer provenance.

## PPU patch fragments

`ppu_affine_front.inc`, `ppu_affine_memo*.inc` and `ppu_cost_cuts.inc` are
inserted into the generated `port/ntr/ppu.cpp` by
`tools/prepare_integrated_native_renderer.py`; they are not compiled on their
own. Their equivalence with the pinned PPU is checked by
`tests/native_renderer/test_ppu_cost_cuts.py`.

## Build pipeline and delivery

- [`pipeline/`](pipeline/README.md): build, run, compare and package
  candidates.
- `deliver_private_cia.py` with `lan_server.py`: serve one verified
  native-test CIA on the local network for the owner's console (FBI QR
  install). Never a public upload.
