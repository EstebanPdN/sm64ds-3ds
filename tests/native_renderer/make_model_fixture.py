"""Author an original bank for host/ARM tests; no ROM or extracted data."""
import hashlib
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from native_renderer.bank import Mesh, Image, Material, Part, build, tiled_rgba8


def fixture(image_count=1):
    vertices = b''.join(struct.pack('<5f4B',*p,255,255,255,255) for p in
        ((0,0,0,0,0),(1,0,0,1,0),(1,1,0,1,1),(0,1,0,0,1)))
    image = tiled_rgba8(bytes([255,255,255,255])*64,8,8)
    return build([Mesh(vertices,struct.pack('<6H',0,1,2,0,2,3))],[Image(8,8,image)]*image_count,
        [Material(texture=0,cull=0)],[Part(0,0,0,0,6)],1,hashlib.sha256(b'Authored native model fixture v2').digest())


if __name__=='__main__':
    output=Path(sys.argv[1]);data=fixture()
    if output.suffix=='.h':
        output.write_text('#pragma once\n#include <cstdint>\nalignas(4) static const std::uint8_t native_model_fixture[]={\n'+
            ','.join(str(v) for v in data)+'\n};\n'+
            'alignas(4) inline constexpr std::uint8_t native_residency_fixture[]={\n'+
            ','.join(str(v) for v in fixture(2))+'\n};\n')
    else:output.write_bytes(data)
