"""Host authoring of native RGBA8/A4 image-only banks; no source GPU state."""
from dataclasses import dataclass
import struct
import zlib

try:
    from .bank import Image, dimensions
except ImportError:
    from bank import Image, dimensions


@dataclass(frozen=True)
class Entry:
    image: Image
    palette_zero_mask: bool = False
    texture_format: int = 0  # native RGBA8=0, A4=1


def build(entries, source_sha256):
    if not 1 <= len(entries) <= 128 or len(source_sha256) != 32:
        raise ValueError('Image bank count/identity bounds')
    output = bytearray((128+len(entries)*32+127)&~127)
    version=2 if any(entry.texture_format for entry in entries) else 1
    gpu_bytes = 0
    for i, entry in enumerate(entries):
        image = entry.image
        if (not dimensions(image.width,image.height) or
                type(entry.texture_format) is not int or entry.texture_format not in (0,1) or
                len(image.tiled_abgr) != (image.width*image.height*4 if entry.texture_format==0 else image.width*image.height//2) or
                not isinstance(entry.palette_zero_mask,bool)):
            raise ValueError('Invalid native image')
        at = len(output)
        output.extend(image.tiled_abgr)
        output.extend(bytes((-len(output))%128))
        gpu_bytes += len(image.tiled_abgr)
        if len(output) > 64*1024*1024:
            raise ValueError('Image bank exceeds file bound')
        struct.pack_into('<8I',output,128+i*32,at,len(image.tiled_abgr),
                         image.width,image.height,int(entry.palette_zero_mask),entry.texture_format,0,0)
    output[:8] = b'SM64NIM1' if version==1 else b'SM64NIM2'
    struct.pack_into('<6I',output,8,version,len(output),len(entries),128,gpu_bytes,0)
    output[32:64] = source_sha256
    struct.pack_into('<I',output,28,zlib.crc32(output))
    return bytes(output)
