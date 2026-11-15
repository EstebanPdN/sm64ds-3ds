"""ROM-free image-only bank used for ownership and ordered-scene GPU checks."""
import hashlib
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from native_renderer.bank import Image,tiled_rgba8
from native_renderer.image_bank import Entry,build


def fixture():
    pixels=[bytes((255,0,0,255))*64,bytes((0,255,0,255))*64,
            b''.join(bytes((255,255,0,0) if x<4 else (0,0,255,255)) for _ in range(8) for x in range(8))]
    return build([Entry(Image(8,8,tiled_rgba8(raw,8,8)),i==2) for i,raw in enumerate(pixels)],
                 hashlib.sha256(b'Authored native image-only residency fixture v1').digest())


if __name__=='__main__':
    data=fixture();Path(sys.argv[1]).write_text('#pragma once\n#include <cstdint>\nalignas(4) static const std::uint8_t native_image_fixture[]={\n'+
        ','.join(str(v) for v in data)+'\n};\n')
