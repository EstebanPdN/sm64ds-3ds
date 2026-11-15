"""Offline original menu member resolution, including explicitly indexed NARCs.

Never imported by the game. Addresses below identify reviewed ROM tables, not
runtime VRAM/graphics state. Callers pin their supported ROM and source policy.
"""
from dataclasses import dataclass
import hashlib
import struct
try:
    from .audit_models import SourceError, span, u32
    from .texture_sequence import string
except ImportError:
    from audit_models import SourceError, span, u32
    from texture_sequence import string


@dataclass(frozen=True)
class Member:
    handle: int
    path: str
    data: bytes
    source_sha256: str


def menu_member(rom, index, *, decompress=False):
    if type(index) is not int or not 1 <= index < 46:
        raise SourceError('Original menu member index')
    overlays = rom.loadArm9Overlays([0, 7])
    menu, names = overlays[7], overlays[0]
    handle = struct.unpack('<H', span(menu.data, 0x020d7584-menu.ramAddress+index*2, 2))[0]
    if handle < 0x8000:
        if handle >= 0x80a:
            raise SourceError('Menu handle outside original filename table')
        at = u32(names.data, 0x020bd4b8-names.ramAddress+handle*4)-names.ramAddress
        path = string(names.data, at, 0).decode('ascii')
        identity = rom.filenames.idOf(path)
        if identity is None or not 0 <= identity < len(rom.files):
            raise SourceError('Menu member path missing from ROM')
        data = bytes(rom.files[identity])
    else:
        import ndspy.codeCompression
        import ndspy.narc
        image = ndspy.codeCompression.decompress(rom.arm9)
        ranges = []
        for slot in range(13):
            row = span(image, 0x0208ecf4-rom.arm9RamAddress+slot*20, 20)
            _, _, first, end, _, path_pointer = struct.unpack('<IIHHII', row)
            if first != 0x8000+slot*0x400 or not first <= end <= first+0x400:
                raise SourceError('Original archive member range changed')
            archive = string(image, path_pointer-rom.arm9RamAddress, 0).decode('ascii').lstrip('/')
            ranges.append((first, end, archive))
        matches = [row for row in ranges if row[0] <= handle < row[1]]
        if len(matches) != 1:
            raise SourceError('Menu archive handle outside counted ranges')
        first, end, archive = matches[0]
        identity = rom.filenames.idOf(archive)
        if identity is None or not 0 <= identity < len(rom.files):
            raise SourceError('Original menu archive missing')
        container = ndspy.narc.NARC(bytes(rom.files[identity]))
        if len(container.files) != end-first:
            raise SourceError('Archive member count differs from original range')
        item = handle-first
        name = container.filenames.filenameOf(item)
        if not name:
            raise SourceError('Original menu archive member has no name')
        data = bytes(container.files[item])
        path = archive+':'+name
    if not data or len(data) > 16*1024*1024:
        raise SourceError('Menu member size bound')
    digest = hashlib.sha256(data).hexdigest()
    if decompress:
        import ndspy.lz10
        if len(data) < 4 or data[0] != 0x10 or not 0 < int.from_bytes(data[1:4], 'little') <= 16*1024*1024:
            raise SourceError('Reviewed raw LZ10 menu member expected')
        expected = int.from_bytes(data[1:4], 'little')
        try:
            data = bytes(ndspy.lz10.decompress(data))
        except (ValueError, IndexError, struct.error) as error:
            raise SourceError('Truncated menu member compression') from error
        if len(data) != expected:
            raise SourceError('Menu member decompressed extent')
    return Member(handle, path, data, digest)
