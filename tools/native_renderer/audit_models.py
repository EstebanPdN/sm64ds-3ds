#!/usr/bin/env python3
"""Inventory original BMD producer contracts before native asset conversion.

Host only. Never linked into the game or the new renderer. This scans the
owner's local ROM and emits metadata, not game resources. No runtime graphics
command interpretation is added by this tool.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
PARAMETERS = {
    0x00: 0, 0x10: 1, 0x11: 0, 0x12: 1, 0x13: 1, 0x14: 1,
    0x15: 0, 0x16: 16, 0x17: 12, 0x18: 16, 0x19: 12, 0x1a: 9,
    0x1b: 3, 0x1c: 3, 0x20: 1, 0x21: 1, 0x22: 1, 0x23: 2,
    0x24: 1, 0x25: 1, 0x26: 1, 0x27: 1, 0x28: 1, 0x29: 1,
    0x2a: 1, 0x2b: 1, 0x30: 1, 0x31: 1, 0x32: 1, 0x33: 1,
    0x34: 32, 0x40: 1, 0x41: 0, 0x50: 1, 0x60: 1,
    0x70: 3, 0x71: 2, 0x72: 1,
}


class SourceError(ValueError):
    pass


def span(data, offset, count, stride=1):
    if offset < 0 or count < 0 or stride <= 0 or offset > len(data) or count > (len(data)-offset)//stride:
        raise SourceError(f'Out-of-file range offset={offset} count={count} stride={stride}')
    return memoryview(data)[offset:offset+count*stride]


def u32(data, at):
    return struct.unpack('<I', span(data, at, 4))[0]


def commands(data):
    if len(data) % 4:
        raise SourceError('Unaligned geometry source length')
    at = 0
    totals = Counter()
    vertices = primitives = 0
    bindings = set()
    initial_matrix = True
    saw_matrix = False
    while at < len(data):
        opcodes = bytes(span(data, at, 4))
        at += 4
        for opcode in opcodes:
            if opcode not in PARAMETERS:
                raise SourceError(f'Unknown geometry command 0x{opcode:02x}')
            args = span(data, at, PARAMETERS[opcode], 4)
            at += len(args)
            if not opcode:
                continue
            totals[f'{opcode:02x}'] += 1
            if opcode == 0x14:
                saw_matrix = True
                bindings.add(struct.unpack('<I', args)[0] & 31)
            if 0x23 <= opcode <= 0x28:
                if vertices == 0:
                    initial_matrix = not saw_matrix
                vertices += 1
            if opcode == 0x40:
                primitives += 1
    return dict(commands=dict(totals), vertices=vertices, primitives=primitives,
                matrix_slots=sorted(bindings), inherits_initial_matrix=initial_matrix if vertices else False)


def audit_bmd(data, name):
    span(data, 0, 0x38)
    if len(data) > 16*1024*1024:
        raise SourceError('Model exceeds the audit bound')
    counts = [u32(data, p) for p in (4, 12, 20, 28, 36)]
    bones, groups, textures, palettes, materials = counts
    if any(n > 65536 for n in counts):
        raise SourceError('Unbounded model table')
    for count, offset, stride in ((bones,8,64),(groups,16,8),(textures,24,20),
                                   (palettes,32,16),(materials,40,48)):
        span(data, u32(data,offset), count, stride)
    parts = []
    totals = Counter()
    table = u32(data,16)
    for group in range(groups):
        count, address = u32(data,table+group*8), u32(data,table+group*8+4)
        if count > 65536:
            raise SourceError('Unbounded part table')
        span(data,address,count,16)
        for part in range(count):
            entry = address+part*16
            matrix_count, matrix_address, size, source = struct.unpack('<4I',span(data,entry,16))
            if matrix_count > 32:
                raise SourceError('More than 32 logical matrix slots')
            matrix_ids = list(span(data,matrix_address,matrix_count))
            result = commands(span(data,source,size))
            if any(slot >= matrix_count for slot in result['matrix_slots']):
                raise SourceError('Geometry references a slot absent from its part palette')
            totals.update(result['commands'])
            parts.append(dict(group=group,part=part,source_bytes=size,matrix_ids=matrix_ids,**result))
    formats = Counter()
    for i in range(textures):
        entry = u32(data,24)+i*20
        address,size = u32(data,entry+4),u32(data,entry+8)
        span(data,address,size)
        formats[str((u32(data,entry+16)>>26)&7)] += 1
    for i in range(palettes):
        entry = u32(data,32)+i*16
        span(data,u32(data,entry+4),u32(data,entry+8))
    # A count is coverage evidence only. Materials/pose inheritance and source
    # state mutations still have to be converted and validated by the cooker.
    return dict(name=name,sha256=hashlib.sha256(data).hexdigest(),bytes=len(data),
                bones=bones,groups=groups,textures=textures,palettes=palettes,materials=materials,
                scale_shift=u32(data,0),texture_formats=dict(formats),commands=dict(totals),
                vertices=sum(p['vertices'] for p in parts),parts=parts,rendering_ready=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom',type=Path,required=True)
    parser.add_argument('--output',type=Path,default=ROOT/'build/native-renderer/model-audit.json')
    args = parser.parse_args()
    if not args.output.resolve().is_relative_to((ROOT/'build').resolve()):
        parser.error('Private source evidence must remain under the ignored build/ directory')
    import ndspy.rom
    import ndspy.lz10
    source = args.rom.read_bytes()
    if hashlib.sha1(source).hexdigest() != '1367529f2cb23e76ef295cb1727333ae8f0a6cd7':
        parser.error('Expected the supported EU revision 0 ROM')
    rom = ndspy.rom.NintendoDSRom(source)
    records, failures = [], []
    for file_id, packed in enumerate(rom.files):
        name = rom.filenames.filenameOf(file_id)
        if not name or not name.lower().endswith('.bmd'):
            continue
        try:
            if packed.startswith(b'LZ77'):
                if len(packed) < 8 or packed[4] != 0x10 or int.from_bytes(packed[5:8],'little') > 16*1024*1024:
                    raise SourceError('Unsupported or oversized compressed model')
                data = ndspy.lz10.decompress(packed[4:])
            else:
                data = packed
            records.append(dict(file_id=file_id,packed_bytes=len(packed),**audit_bmd(data,name)))
        except (SourceError, ValueError, IndexError, struct.error) as error:
            failures.append(dict(file_id=file_id,name=name,error=str(error)))
    total = Counter()
    for record in records:
        total.update(record['commands'])
    report = dict(scope='Original BMD geometry/resource contracts for the native cooker; no runtime or rendering acceptance',
                  rom_sha1=hashlib.sha1(source).hexdigest(),models=len(records),failures=failures,
                  commands=dict(sorted(total.items())),vertices=sum(r['vertices'] for r in records),
                  records=records,rendering_ready=False)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('models','failures','commands','vertices','rendering_ready')},indent=2))
    return 0 if not failures else 1


if __name__ == '__main__':
    raise SystemExit(main())
