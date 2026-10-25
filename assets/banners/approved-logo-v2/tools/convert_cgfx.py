"""Convert the actual logo geometry to CGFX, retaining material lighting and valid names."""
from pathlib import Path
import sys,json,struct,importlib.util,hashlib
ROOT=Path(__file__).resolve().parents[1]
PROJECTS=ROOT.parents[3]
PYCGFX=PROJECTS/'THE MINISH CAP/assets/banners/final_banner/tools/pycgfx'
sys.path.insert(0,str(PYCGFX))
import gltflib
import main as converter
from cgfx.sobj import BillboardMode
gltf=gltflib.GLTF.load(str(ROOT/'ready-to-use'/'sm64ds-logo.glb'),load_file_resources=True)
assert not gltf.model.animations
assert not gltf.model.images,'The logo must not use its reference as a texture'
cgfx=converter.convert_gltf(gltf)
model=cgfx.data.models['COMMON']
assert set(model.skeleton.bones)=={'Scene root','Logo'}
bone=model.skeleton.bones['Logo'];bone.billboard_mode=BillboardMode.YAxial
for mesh in model.meshes.data.contents:mesh.mesh_node_name='Logo'
model.skeleton.bones.dict.regenerate()
blob=converter.write(cgfx)
assert len(blob)<524288,len(blob)
path=ROOT/'ready-to-use'/'sm64ds-logo.cgfx';path.write_bytes(blob)
validator=PROJECTS/'STARFOX SNES/Source/platform/3ds/tools/validate-cgfx-dicts.py'
spec=importlib.util.spec_from_file_location('dict_check',validator)
mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
report=mod.validate(path.read_bytes());assert not report['failures'],report
assert struct.unpack_from('<I',blob,bone.offset+212)[0]==5
for mesh in model.meshes.data.contents:
    field=mesh.offset+mesh.struct.size-4
    name=field+struct.unpack_from('<i',blob,field)[0]
    assert blob[name:blob.index(0,name)]==b'Logo'
stats=json.loads((ROOT/'ready-to-use'/'validation.json').read_text())
stats.update(cgfx_bytes=len(blob),cgfx_limit_bytes=524288,cgfx_headroom_bytes=524288-len(blob),cgfx_sha256=hashlib.sha256(blob).hexdigest(),serialized_dictionary_audit=report,cgfx_logo_billboard='YAxial',cgfx_textures=0,cgfx_animation=False,physical_banner_tested=False)
(ROOT/'ready-to-use'/'validation.json').write_text(json.dumps(stats,indent=2)+'\n')
print('CGFX:',len(blob),'bytes; zero reference-image textures; dictionaries valid; physical test pending.')
