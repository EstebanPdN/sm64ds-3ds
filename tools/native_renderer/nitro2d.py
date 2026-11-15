"""Host-only original menu 2D resource reader. Never linked into the game.

Reads counted NCCG/NCCL/NCOB containers used by the pinned menu constructors.
Returns semantic cells and prepared RGBA pixels, without a runtime OAM/VRAM
interpreter. Resource associations still require an explicit reviewed profile.
"""
from dataclasses import dataclass
import struct

try:
    from .audit_models import SourceError, span, u32
except ImportError:
    from audit_models import SourceError, span, u32


def blocks(data, magic, version):
    header = bytes(span(data, 0, 16))
    kind, order, revision, size, start, count = struct.unpack('<4sHHIHH', header)
    if kind != magic or order != 0xfeff or revision != version or size != len(data) or start != 16 or not 1 <= count <= 16:
        raise SourceError('Menu 2D container header/version/extent')
    result = {}
    offset = start
    for _ in range(count):
        tag, extent = struct.unpack('<4sI', span(data, offset, 8))
        if tag in result or extent < 8 or extent % 4:
            raise SourceError('Menu 2D duplicate/unaligned block')
        result[tag] = bytes(span(data, offset + 8, extent - 8))
        offset += extent
    if offset != size:
        raise SourceError('Menu 2D blocks do not cover resource')
    return result


@dataclass(frozen=True)
class TileMap:
    width: int
    height: int
    fields: tuple
    entries: tuple


def tilemap(data):
    parts = blocks(data, b'NCSC', 0x101)
    if b'SCRN' not in parts:
        raise SourceError('Missing background screen block')
    screen = parts[b'SCRN']
    width, height, first, second = struct.unpack('<4I', span(screen, 0, 16))
    if not 1 <= width <= 128 or not 1 <= height <= 128 or first != 0 or second not in (0, 1):
        raise SourceError('Unreviewed background screen dimensions/fields')
    if len(screen) != 16 + width*height*2:
        raise SourceError('Background screen entry extent')
    return TileMap(width, height, (first, second), struct.unpack_from('<'+str(width*height)+'H', screen, 16))


@dataclass(frozen=True)
class Characters:
    width: int
    height: int
    eight_bit: bool
    pixels: bytes

    def index(self, x, y):
        if not 0 <= x < self.width or not 0 <= y < self.height:
            raise SourceError('Character coordinate outside prepared atlas')
        tile = (y // 8) * (self.width // 8) + x // 8
        at = tile * (64 if self.eight_bit else 32) + (y % 8) * (8 if self.eight_bit else 4)
        at += x % 8 if self.eight_bit else (x % 8) // 2
        value = self.pixels[at]
        return value if self.eight_bit else (value >> (4 * (x % 2))) & 15


def characters(data):
    parts = blocks(data, b'NCCG', 0x100)
    if b'CHAR' not in parts:
        raise SourceError('Missing character block')
    data = parts[b'CHAR']
    width, height, eight = struct.unpack('<3I', span(data, 0, 12))
    if not 1 <= width <= 128 or not 1 <= height <= 128 or eight not in (0, 1):
        raise SourceError('Unsupported character dimensions/format')
    size = width * height * (64 if eight else 32)
    if len(data) != 12 + size:
        raise SourceError('Character pixel extent')
    return Characters(width * 8, height * 8, bool(eight), data[12:])


def palette(data):
    parts = blocks(data, b'NCCL', 0x100)
    if b'PALT' not in parts:
        raise SourceError('Missing palette block')
    data = parts[b'PALT']
    colours, banks = struct.unpack('<2I', span(data, 0, 8))
    if colours not in (16, 256) or not 1 <= banks <= 16 or colours * banks != 256 or len(data) != 520:
        raise SourceError('Unsupported palette count/extent')
    return data[8:]


def rgba(source, colours, bank=0):
    if len(colours) != 512 or not 0 <= bank < 16:
        raise SourceError('Palette bank/extent')
    output = bytearray(source.width * source.height * 4)
    for y in range(source.height):
        for x in range(source.width):
            index = source.index(x, y)
            value = struct.unpack_from('<H', colours, (index if source.eight_bit else bank * 16 + index) * 2)[0]
            rgb = [((value >> shift) & 31) for shift in (0, 5, 10)]
            rgb = [(v << 3) | (v >> 2) for v in rgb]
            at = (y * source.width + x) * 4
            output[at:at+4] = bytes(rgb + [255 if index else 0])
    return bytes(output)


@dataclass(frozen=True)
class CellPart:
    x: int
    y: int
    width: int
    height: int
    tile: int
    palette: int
    priority: int
    flip_x: bool
    flip_y: bool
    affine_index: int
    affine: bool
    double_size: bool
    hidden: bool
    mode: int
    mosaic: bool
    eight_bit: bool


WIDTH = ((8, 16, 32, 64), (16, 32, 32, 64), (8, 8, 16, 32))
HEIGHT = ((8, 16, 32, 64), (8, 8, 16, 32), (16, 32, 32, 64))


def cells(data):
    parts = blocks(data, b'NCOB', 0x102)
    if b'CELL' not in parts:
        raise SourceError('Missing cell block')
    data = parts[b'CELL']
    count = u32(data, 0)
    if not 1 <= count <= 128:
        raise SourceError('Cell count bound')
    offset = 4
    result = []
    for _ in range(count):
        parts = u32(data, offset)
        offset += 4
        if not 1 <= parts <= 128:
            raise SourceError('Cell part count bound')
        cell = []
        for _ in range(parts):
            v = struct.unpack('<hh12BHH', span(data, offset, 20))
            offset += 20
            x, y, unused, affine_index, fx, fy, obj_mode, mode, mosaic, eight, shape, size, priority, pal, tile, extra = v
            if shape > 2 or size > 3 or priority > 3 or pal > 15 or tile > 1023 or mode > 3 or obj_mode > 3 or affine_index > 31:
                raise SourceError('Unsupported source cell field')
            if any(value not in (0, 1) for value in (fx, fy, mosaic, eight)):
                raise SourceError('Unsupported source cell boolean')
            # Original producer wraps x/y to hardware position domains. Retain
            # signed semantic coordinates here; the runtime pose handles wrap.
            cell.append(CellPart(x, y, WIDTH[shape][size], HEIGHT[shape][size], tile, pal, priority,
                bool(fx), bool(fy), affine_index, bool(obj_mode & 1), obj_mode == 3, obj_mode == 2, mode, bool(mosaic), bool(eight)))
        result.append(tuple(cell))
    if offset != len(data):
        raise SourceError('Cell records do not cover block')
    return tuple(result)
