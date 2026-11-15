"""A census must distinguish normal-basis changes, light changes and barriers."""
import copy
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tools/native_renderer'))
from bank import Mesh,Material,Part,build

with tempfile.TemporaryDirectory() as directory:
    work=Path(directory);capture=work/'capture.json'
    subprocess.run([sys.argv[1],str(capture)],check=True)
    scene=json.loads(capture.read_text())
    scene['occurrences']=[[17,0,0,3,0],[1,1,0,4,0],[17,0,3,3,0]]
    scene['image_layers']=[];scene['scene_order']=0
    part=scene['parts'][0];part['image']=2**32-1;part['material']['uv_mode']=0
    scene['parts']=[copy.deepcopy(part) for _ in range(6)]
    state=scene['lighting_states'][0];other=copy.deepcopy(state);other['normal'][0]=-4096
    different=copy.deepcopy(state);different['material'][0]=12345
    scene['lighting_states']=[state,other,different]
    for i,p in enumerate(scene['parts']):p['lighting']=i%3
    vertices=b''.join(struct.pack('<5f4B4h',x,y,0,0,0,255,255,255,255,0,0,511,2) for x,y in [(0,0),(1,0),(0,1)])
    blob=build([Mesh(vertices,struct.pack('<3H',0,1,2),True)],[],[Material()],
        [Part(0,0,0,0,3)]*3,1,bytes(32))
    (work/'00017.nmd').write_bytes(blob);capture.write_text(json.dumps(scene))
    report=work/'report.json'
    command=[sys.executable,str(root/'tools/native_renderer/analyze_component_batches.py'),
        str(capture),str(work),str(report),'--contracts',sys.argv[2],'--compiler',sys.argv[3],*sys.argv[4:]]
    subprocess.run(command,check=True,stdout=subprocess.DEVNULL)
    result=json.loads(report.read_text())
    assert (result['world_draws'],result['quad_barriers'],result['vertices_per_part_unique'])==(6,1,18)
    attributes,prelit=result['candidates']
    assert (attributes['world_draws_after'],attributes['draws_removed'],attributes['maximum_run'])==(4,2,2)
    assert (prelit['world_draws_after'],prelit['draws_removed'],prelit['maximum_run'])==(2,4,3)
    # Adjacent texture/material changes remain barriers even with prelighting.
    scene['parts'][1]['material']['tint']=0x123456ff;capture.write_text(json.dumps(scene))
    subprocess.run(command,check=True,stdout=subprocess.DEVNULL)
    changed=json.loads(report.read_text())
    assert changed['candidates'][1]['world_draws_after']==4
print('PASS: normal basis versus lighting source, per-material barriers, quad order and unique vertex counts')
