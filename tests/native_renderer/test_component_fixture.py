"""Check typed diagnostic fixture generation with a complete authored capture."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("component_fixture", root / "tools/native_renderer/component_fixture.py")
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "scene.json"
    subprocess.run([sys.argv[1], str(path)], check=True)
    scene = json.loads(path.read_text())
    skin_scene = json.loads(Path(str(path)+'.skin.json').read_text())
header = fixture.make_fixture(scene)
assert header.startswith('#pragma once\n#include "scene_frame.hpp"\n')
assert '18446744073709551615ull' in header
assert 'quad_states[0].polygon_id=35;' in header and 'quad_states[0].fog=true;' in header
assert 'parts[0].material.fixed_uv.offset={-32768,0};' in header
assert 'light_sets[0].light[0].direction={-1024,511,0};' in header
assert 'parts[0].lighting=0;' in header and 'lighting_states[0].lights=0;' in header
assert 'image_layers[0].bank_id=4096u;' in header and 'image_layers[0].rectangle={0.0f,160.0f,320.0f,20.0f};' in header
assert 'image_layers.data(),image_layers.size(),1408,Screen(1)' in header
version2 = copy.deepcopy(scene);version2['format'] = 'SM64_NATIVE_COMPONENT_SCENE_2'
for state in version2['lighting_states']:state['material']=state['material'][:6]
for layer in version2['image_layers']:
    del layer['source'];del layer['part']
assert 'image_layers[0].source=LayerSource(0);' in fixture.make_fixture(version2)
canvas = copy.deepcopy(scene);canvas['image_layers'][0].update(source=1, part=3, image=0)
assert 'image_layers[0].source=LayerSource(1);' in fixture.make_fixture(canvas)
clipped = copy.deepcopy(scene);clipped['image_layers'][0]['clip'] = [7, 13, 303, 233]
assert 'image_layers[0].clip={7,13,303,233};' in fixture.make_fixture(clipped)
version3 = copy.deepcopy(scene);version3['format'] = 'SM64_NATIVE_COMPONENT_SCENE_3'
for state in version3['lighting_states']:state['material']=state['material'][:6]
for layer in version3['image_layers']:del layer['clip']
assert 'image_layers[0].clip={0,0,0,0};' in fixture.make_fixture(version3)
legacy = copy.deepcopy(scene)
legacy.update(format='SM64_NATIVE_COMPONENT_SCENE_1', scope='native-3d-component')
for state in legacy['lighting_states']:state['material']=state['material'][:6]
for field in ['image_layers', 'scene_screen', 'scene_order']: del legacy[field]
assert 'image_layers' not in fixture.make_fixture(legacy)
skin_header=fixture.make_fixture(skin_scene)
assert 'std::array<NormalVertex,6> skin_vertices' in skin_header
assert 'Occurrence::Kind(2)' in skin_header and 'Occurrence::Kind(3)' in skin_header
assert 'skin_vertices[5]={{5.0f,-2.0f,3.0f,0.25f,0.75f,17,23,31,255},-512,511,-1,2};' in skin_header
assert 'parts[1].highlight=true;' in skin_header and 'parts[1].material.blend=Blend(3);' in skin_header
assert 'light_sets[0].toon_known=true;' in skin_header
assert 'skin_vertices.data(),skin_vertices.size()' in skin_header
assert 'image_layers[1].source=LayerSource(2);' in skin_header and 'image_layers[1].tone_target=32;' in skin_header
assert 'image_layers[0].tone_target=16;' in header
version4=copy.deepcopy(scene);version4['format']='SM64_NATIVE_COMPONENT_SCENE_4'
for state in version4['lighting_states']:state['material']=state['material'][:6]
assert 'parts[0].highlight=false;' in fixture.make_fixture(version4)
bad = []
value = copy.deepcopy(scene);value['original_scene_accepted'] = True;bad.append(value)
value = copy.deepcopy(scene);value['physical_capture'] = True;bad.append(value)
value = copy.deepcopy(scene);value['scope'] = 'whole-game';bad.append(value)
value = copy.deepcopy(scene);value['quad_vertices'][3][0] = float('nan');bad.append(value)
value = copy.deepcopy(scene);value['quad_states'][0]['material']['tint'] = '0; injected();';bad.append(value)
value = copy.deepcopy(scene);value['quad_states'][0]['bank'] = 2;bad.append(value)
value = copy.deepcopy(scene);value['light_sets'][0]['generation'] = -1;bad.append(value)
value = copy.deepcopy(scene);value['parts'] *= 2049;bad.append(value)
value = copy.deepcopy(scene);value['image_layers'][0]['screen'] = 2;bad.append(value)
value = copy.deepcopy(scene);value['image_layers'][0]['image'] = 128;bad.append(value)
value = copy.deepcopy(scene);value['scene_screen'] = -1;bad.append(value)
value = copy.deepcopy(scene);value['image_layers'] *= 128;bad.append(value)
value = copy.deepcopy(scene);value['image_layers'][0]['source'] = 2;bad.append(value)
value = copy.deepcopy(scene);value['image_layers'][0]['part'] = 1;bad.append(value)
for clip in [[0,0,321,240],[0,0,320,241],[10,5,9,6],[0,0,0,1],[-1,0,320,240]]:
    value = copy.deepcopy(scene);value['image_layers'][0]['clip'] = clip;bad.append(value)
for normal, source in [(512, 0),(-513, 0),(0, 3)]:
    value=copy.deepcopy(skin_scene);value['skin_vertices'][0][9]=normal;value['skin_vertices'][0][12]=source;bad.append(value)
value=copy.deepcopy(skin_scene);value['skin_vertices']*=2048;bad.append(value)
value=copy.deepcopy(skin_scene);value['lighting_states'][0]['material'][6]=2;bad.append(value)
value=copy.deepcopy(skin_scene);value['image_layers'][1]['tone_target']=3;bad.append(value)
for value in bad:
    try:
        fixture.make_fixture(value)
    except ValueError:
        continue
    raise AssertionError('Invalid component accepted')
print('PASS: preserved semantic policies, typed C++ literals, bounds and invalid-input refusal')
