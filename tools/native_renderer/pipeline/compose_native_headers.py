"""Compose the compiled native catalog headers exactly as the lost checkout had them.

Inputs are the cooked catalog directories (default: the original build/ paths; cook
them first, see RECOVERY.md).  Output: OUT/native_{model,image,menu_cell,menu_obj,
menu_backdrop,canvas,background}_catalog.hpp, ready for
  python3 tools/prepare_integrated_native_renderer.py --source <checkout> --runtime \
     --catalog OUT/native_model_catalog.hpp --image-catalog OUT/native_image_catalog.hpp \
     --menu-catalog OUT/native_menu_cell_catalog.hpp --menu-obj-catalog OUT/native_menu_obj_catalog.hpp \
     --menu-backdrop-catalog OUT/native_menu_backdrop_catalog.hpp \
     --canvas-catalog OUT/native_canvas_catalog.hpp --background-catalog OUT/native_background_catalog.hpp
Run from the project root with plain python3 (only tools/native_renderer/particles.py is imported).
"""
import argparse
import json
import re
import sys
from pathlib import Path

root = Path.cwd()
nr = root / 'build/native-renderer'
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--out', type=Path, required=True)
ap.add_argument('--model', type=Path, default=nr / 'playable-r1/catalog-approx-r8/native_catalog.hpp',
                help='SM64-3 #1158 staged catalog-approx-r8/native_catalog.hpp (exact copy also in recovered/romfs-from-cia107/native/)')
ap.add_argument('--particles', type=Path, default=nr / 'catalog-particles-20261027-r1')
ap.add_argument('--menu-frame', type=Path, default=nr / 'menu-frame-r1/catalog')
ap.add_argument('--menu-frame-r2', type=Path, default=nr / 'menu-frame-r1/catalog-r2')
ap.add_argument('--font', type=Path, default=nr / 'text-r1/font-r1')
ap.add_argument('--menu-obj', type=Path, default=nr / 'menu-obj-r1/catalog-r2')
ap.add_argument('--live-backgrounds', type=Path, default=nr / 'live-backgrounds-r1/catalog-r1')
ap.add_argument('--backdrops-canvas', type=Path, default=nr / 'menu-backdrops-r1/catalog-r4',
                help='SM64-3 #3194 merged the r4 canvas header; #3416 asserted r7 banks are byte-identical to r4')
ap.add_argument('--backdrops', type=Path, default=nr / 'menu-backdrops-r1/catalog-r7')
a = ap.parse_args()
a.out.mkdir(parents=True, exist_ok=False)
sys.path.insert(0, str(root / 'tools/native_renderer'))
from particles import catalog_header  # same import as SM DEV #5066

# 1. model catalog (SM64-3 #1158): verbatim copy of the author_catalog output.
(a.out / 'native_model_catalog.hpp').write_bytes(a.model.read_bytes())

# 2. image catalog, legacy part (SM DEV #5066): particles banks 0/1 + menu frame 4096.
particles = json.loads((a.particles / 'catalog.json').read_text())
menu = json.loads((a.menu_frame / 'catalog.json').read_text())
h = catalog_header(particles['records'] + [menu])

# 3. font bank 6000 appended (SM64-3 #2678).
report = json.loads((a.font / 'report.json').read_text()); assert 'bank_6000_images' not in h
h = h.replace('inline constexpr ImageBankBinding banks[]=', 'inline constexpr ImageAsset bank_6000_images[]={{256,128,ImageBank::Alpha::independent,TextureFormat::alpha4}};\ninline constexpr ImageBankBinding banks[]=', 1)
m = re.search(r'(inline constexpr ImageBankBinding banks\[\]=\{)(.*)(\};)', h); assert m
binding = '{6000u,{' + ','.join(str(v) for v in bytes.fromhex(report['source_sha256'])) + '},bank_6000_images,1u}'
h = h[:m.start(2)] + m[2] + ',' + binding + h[m.end(2):]

# 4. menu OBJ image banks merged (SM64-3 #3041).
legacy = h; new = (a.menu_obj / 'native_image_catalog.hpp').read_text()
assert legacy.count('ImageBankBinding banks[]') == 1 and new.count('ImageBankBinding banks[]') == 1
legacy = legacy.replace('ImageBankBinding banks[]', 'ImageBankBinding legacy_banks[]'); new = new.replace('ImageBankBinding banks[]', 'ImageBankBinding obj_banks[]')
ids = re.findall(r'inline constexpr ImageAsset bank_(\d+)_images', legacy); assert ids == ['0', '1', '4096', '6000'], ids
merged = legacy + '\n' + new + '\nnamespace sm64ds::render::prepared_images {\ninline constexpr ImageBankBinding banks[]={' + ','.join(['legacy_banks[' + str(i) + ']' for i in range(4)] + ['obj_banks[' + str(i) + ']' for i in range(20)]) + '};\n}\n'

# 5. bank order fixed (SM64-3 #3290): font bank last.
before = 'legacy_banks[0],legacy_banks[1],legacy_banks[2],legacy_banks[3],' + ','.join(f'obj_banks[{i}]' for i in range(20))
after = 'legacy_banks[0],legacy_banks[1],legacy_banks[2],' + ','.join(f'obj_banks[{i}]' for i in range(20)) + ',legacy_banks[3]'
assert before in merged; merged = merged.replace(before, after)
(a.out / 'native_image_catalog.hpp').write_text(merged)

# 6. typed menu cells (SM DEV #5128 replaced the #5066 copy with catalog-r2; 04096.nim unchanged).
assert (a.menu_frame_r2 / '04096.nim').read_bytes() == (a.menu_frame / '04096.nim').read_bytes()
(a.out / 'native_menu_cell_catalog.hpp').write_bytes((a.menu_frame_r2 / 'native_menu_cell_catalog.hpp').read_bytes())

# 7. menu OBJ views (SM64-3 #3041).
(a.out / 'native_menu_obj_catalog.hpp').write_bytes((a.menu_obj / 'native_menu_obj_catalog.hpp').read_bytes())

# 8. original background binding (SM DEV #5574).
(a.out / 'native_background_catalog.hpp').write_bytes((a.live_backgrounds / 'native_background_catalog.hpp').read_bytes())

# 9. canvas catalog: live background 5001 + 11 backdrop banks (SM64-3 #3194).
old = (a.live_backgrounds / 'native_canvas_catalog.hpp').read_text(); new = (a.backdrops_canvas / 'native_canvas_catalog.hpp').read_text()
assert old.count('CanvasBinding banks[]') == 1 and new.count('CanvasBinding banks[]') == 1
old = old.replace('CanvasBinding banks[]', 'CanvasBinding legacy_banks[]'); new = new.replace('CanvasBinding banks[]', 'CanvasBinding backdrop_banks[]')
merged = old + '\n' + new + '\nnamespace sm64ds::render::prepared_canvases {\ninline constexpr CanvasBinding banks[]={legacy_banks[0],' + ','.join('backdrop_banks[' + str(i) + ']' for i in range(11)) + '};\n}\n'
assert '5001' in old
(a.out / 'native_canvas_catalog.hpp').write_text(merged)

# 10. backdrop views from catalog-r7 (SM64-3 #3416); its GPU banks equal r4.
for p in a.backdrops_canvas.glob('*.nmd'): assert p.read_bytes() == (a.backdrops / p.name).read_bytes(), p
(a.out / 'native_menu_backdrop_catalog.hpp').write_bytes((a.backdrops / 'native_menu_backdrop_catalog.hpp').read_bytes())
print('Composed', sorted(p.name for p in a.out.iterdir()))
