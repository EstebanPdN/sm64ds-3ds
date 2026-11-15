"""Host authoring for SM64NMD2 and optional-normal SM64NMD3 GPU banks.

Only native vertex/index/image data and explicit model/material references
are emitted. This module knows no DS graphics command encodings.
"""
from dataclasses import dataclass
import math
import struct
import zlib


@dataclass(frozen=True)
class Mesh:
    vertices: bytes
    indices: bytes
    normals: bool = False


@dataclass(frozen=True)
class Image:
    width: int
    height: int
    tiled_abgr: bytes


@dataclass(frozen=True)
class Material:
    texture: int | None = None
    blend: int = 0
    cull: int = 1
    depth_test: bool = True
    depth_write: bool = True
    cutoff: int = 0
    uv: tuple = (1,0,0,0,1,0)
    tint: int = 0xffffffff
    wrap_s: int = 0
    wrap_t: int = 0


@dataclass(frozen=True)
class Part:
    mesh: int
    material: int
    matrix: int
    first: int
    count: int


def tiled_rgba8(rgba, width, height):
    if not dimensions(width,height) or len(rgba) != width*height*4:
        raise ValueError('Invalid native image dimensions/bytes')
    output = bytearray(len(rgba))
    for y in range(height):
        for x in range(width):
            gy=height-1-y
            morton = sum(((x>>bit)&1)<<(bit*2) | ((gy>>bit)&1)<<(bit*2+1) for bit in range(3))
            at = (((gy//8)*(width//8)+x//8)*64+morton)*4
            output[at:at+4] = rgba[(y*width+x)*4:(y*width+x+1)*4][::-1]
    return bytes(output)


def dimensions(w,h):
    return 8 <= w <= 1024 and 8 <= h <= 1024 and not(w&(w-1)) and not(h&(h-1))


def build(meshes, images, materials, parts, matrices, source_sha256,require_material_bindings=False):
    if not(1 <= len(meshes) <= 256 and len(images) <= 128 and 1 <= len(materials) <= 512 and
           1 <= len(parts) <= 1024 and 1 <= matrices <= 256) or len(source_sha256) != 32:
        raise ValueError('Native bank count/identity bounds')
    mesh_table = 128
    image_table = mesh_table+len(meshes)*32
    material_table = image_table+len(images)*32
    part_table = material_table+len(materials)*64
    payload = (part_table+len(parts)*32+127)&~127
    output = bytearray(payload)
    for i, mesh in enumerate(meshes):
        vb, ib = len(mesh.vertices), len(mesh.indices)
        stride=32 if mesh.normals else 24
        if not vb or vb%stride or vb//stride>65536 or not ib or ib%6 or ib//2>65535:
            raise ValueError('Invalid native mesh sizes')
        for vertex in struct.iter_unpack('<5f4B4h' if mesh.normals else '<5f4B',mesh.vertices):
            if not all(math.isfinite(x) for x in vertex[:5]):
                raise ValueError('Nonfinite native vertex')
            if mesh.normals and (any(not -512<=v<=511 for v in vertex[9:12]) or vertex[12] not in (0,1,2)):
                raise ValueError('Native normal/offset lattice')
        if any(index[0]>=vb//stride for index in struct.iter_unpack('<H',mesh.indices)):
            raise ValueError('Invalid native index')
        vertex_at = len(output)
        output.extend(mesh.vertices)
        output.extend(bytes((-len(output))&3))
        index_at = len(output)
        output.extend(mesh.indices)
        output.extend(bytes((-len(output))&3))
        struct.pack_into('<8I',output,mesh_table+i*32,vertex_at,vb,index_at,ib,int(mesh.normals),0,0,0)
    for i, image in enumerate(images):
        if not dimensions(image.width,image.height) or len(image.tiled_abgr)!=image.width*image.height*4:
            raise ValueError('Invalid native image')
        output.extend(bytes((-len(output))&127))
        at = len(output)
        output.extend(image.tiled_abgr)
        struct.pack_into('<8I',output,image_table+i*32,at,len(image.tiled_abgr),image.width,image.height,0,0,0,0)
    for i, m in enumerate(materials):
        if ((m.texture is not None and not 0<=m.texture<len(images)) or m.blend not in (0,1) or m.cull not in (0,1,2) or
            not 0<=m.cutoff<=255 or m.wrap_s not in (0,1,2) or m.wrap_t not in (0,1,2)):
            raise ValueError('Invalid native material')
        if len(m.uv)!=6 or not all(math.isfinite(v) for v in m.uv):
            raise ValueError('Invalid native UV transform')
        struct.pack_into('<5I6fI',output,material_table+i*64,0xffffffff if m.texture is None else m.texture,
            m.blend,m.cull,int(m.depth_test)|(int(m.depth_write)<<1)|(m.wrap_s<<2)|(m.wrap_t<<4),m.cutoff,*m.uv,m.tint)
    for i, p in enumerate(parts):
        if not(0<=p.mesh<len(meshes) and 0<=p.material<len(materials) and 0<=p.matrix<matrices and
               p.first>=0 and p.count>0 and not p.first%3 and not p.count%3 and
               p.first+p.count<=len(meshes[p.mesh].indices)//2):
            raise ValueError('Invalid native part reference/range')
        struct.pack_into('<8I',output,part_table+i*32,p.mesh,p.material,p.matrix,p.first,p.count,0,0,0)
    if len(output)>64*1024*1024:
        raise ValueError('Native bank exceeds file bound')
    extended=any(m.normals for m in meshes)
    output[:8] = b'SM64NMD3' if extended else b'SM64NMD2'
    struct.pack_into('<12I',output,8,3 if extended else 2,len(output),len(meshes),len(images),len(materials),len(parts),
        mesh_table,image_table,material_table,part_table,matrices,0)
    output[56:88] = source_sha256
    struct.pack_into('<I',output,88,int(require_material_bindings))
    struct.pack_into('<I',output,52,zlib.crc32(output))
    return bytes(output)
