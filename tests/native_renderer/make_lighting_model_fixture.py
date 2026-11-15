"""ROM-free indexed normal models for the complete snapshot -> GPU chain."""
from pathlib import Path
import hashlib,json,struct,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from native_renderer.bank import Mesh,Material,Part,build

output=Path(sys.argv[1]);cases=json.loads(Path(sys.argv[2]).read_text())['cases'];data=bytearray();records=[]
for i,c in enumerate(cases):
    vertices=b''.join(struct.pack('<5f4B4h',x,y,0,0,0,49,115,181,255,*c['normal'],0) for x,y in [(0,0),(1,0),(1,1),(0,1)])
    digest=hashlib.sha256(f'Authored native lighting case {i}'.encode()).digest()
    blob=build([Mesh(vertices,struct.pack('<6H',0,1,2,0,2,3),True)],[],
        [Material(cull=0,depth_test=False,depth_write=False)],[Part(0,0,0,0,6)],1,digest,True)
    data.extend(bytes((-len(data))%4));records.append((len(data),len(blob),digest));data.extend(blob)
output.write_text('#pragma once\n#include <array>\n#include <cstdint>\nnamespace native_lab_cases {\n'
    'alignas(4) inline constexpr std::uint8_t lighting_model_bytes[]={'+','.join(map(str,data))+'};\n'
    'struct ModelFixture {unsigned offset,bytes;std::array<std::uint8_t,32> sha;};\n'
    'inline constexpr ModelFixture lighting_model_fixtures[]={'+','.join('{'+str(at)+','+str(size)+',{'+','.join(map(str,sha))+'}}' for at,size,sha in records)+'};\n}\n')
