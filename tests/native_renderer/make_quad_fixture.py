"""Author the quad lab's white model and a single reusable zero-colour mask."""
import hashlib
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from native_renderer.bank import Mesh,Image,Material,Part,build,tiled_rgba8


def fixture():
    vertices=b''.join(struct.pack('<5f4B',*v,255,255,255,255) for v in
        [(0,0,0,0,0),(1,0,0,1,0),(1,1,0,1,1),(0,1,0,0,1)])
    mask=b''.join(bytes((255,0,0,0) if x<4 else (0,255,0,255)) for y in range(8) for x in range(8))
    images=[Image(8,8,tiled_rgba8(bytes([255]*256),8,8)),Image(8,8,tiled_rgba8(mask,8,8))]
    return build([Mesh(vertices,struct.pack('<6H',0,1,2,0,2,3))],images,
        [Material(texture=0,cull=0)],[Part(0,0,0,0,6)],1,hashlib.sha256(b'Authored quad alpha-mask fixture v1').digest())


if __name__=='__main__':
    data=fixture();Path(sys.argv[1]).write_text('#pragma once\n#include <cstdint>\nalignas(4) static const std::uint8_t native_model_fixture[]={\n'+
        ','.join(str(v) for v in data)+'\n};\n')
