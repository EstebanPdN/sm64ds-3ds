#!/usr/bin/env python3
"""Packed memory.vm (SM64VMZ1): expand, inspect and pack.

Since BUILD140 the console writes memory.vm / memoria.vm as SM64VMZ1
(native_diagnostics.hpp, write_memory_packed). Expanding one gives back the
SM64VM1 bytes the raw writer would have produced, except that the dump's own
2 MiB workspace reads as zeros. Every reader (inspect_integrated_vm.VM,
read_mapped_capture.MappedCapture) expands transparently.

Layout, little endian:
  PackHeader  16 B  b'SM64VMZ1', u32 version (1), u32 block_bytes (512 KiB)
  Header      64 B  the SM64VM1 header, unchanged
  per region: the 24-byte SM64VM1 Region record, then until its payload_bytes
              are covered: PackBlock 16 B (u32 address, logical, stored, kind)
              followed by `stored` bytes
  kinds: 0 all zero (stored 0), 1 raw (stored == logical),
         2 one raw LZ4 block, no frame (0 < stored < logical),
         3 excluded: the dump workspace (stored 0, expands to zeros)

Pure Python, no third-party module needed to read (Python 3.8+).

  vm_packed.py expand IN OUT [--allow-incomplete]
  vm_packed.py info IN
  vm_packed.py pack IN OUT [--method auto|lz4|raw]
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys

MAGIC = b'SM64VMZ1'
PACK_HEADER = struct.Struct('<8sII')
PACK_BLOCK = struct.Struct('<4I')
VM_HEADER = struct.Struct('<8sIIIIQQIIIIII')
REGION = struct.Struct('<6I')
KIND_ZERO, KIND_RAW, KIND_LZ4, KIND_EXCLUDED = range(4)
KIND_NAMES = ('zero', 'raw', 'lz4', 'excluded')
BLOCK_BYTES = 512 * 1024
MAX_REGIONS = 512


def is_packed(raw):
    return bytes(raw[:8]) == MAGIC


def lz4_block(src, size):
    """Decode one raw LZ4 block (no frame header) that expands to `size` bytes."""
    src = memoryview(src)
    out = bytearray()
    i, n = 0, len(src)
    try:
        while i < n:
            token = src[i]
            i += 1
            literals = token >> 4
            if literals == 15:
                while True:
                    b = src[i]
                    i += 1
                    literals += b
                    if b != 255:
                        break
            if i + literals > n:
                raise ValueError('Corrupt LZ4 block: literals run past the end')
            out += src[i:i + literals]
            i += literals
            if i == n:
                break  # the last sequence has literals only
            if i + 2 > n:
                raise ValueError('Corrupt LZ4 block: truncated match offset')
            offset = src[i] | src[i + 1] << 8
            i += 2
            length = (token & 15) + 4
            if token & 15 == 15:
                while True:
                    b = src[i]
                    i += 1
                    length += b
                    if b != 255:
                        break
            if not 0 < offset <= len(out):
                raise ValueError('Corrupt LZ4 block: match offset outside the output')
            if len(out) + length > size:
                raise ValueError('Corrupt LZ4 block: output longer than declared')
            start = len(out) - offset
            if length <= offset:
                out += out[start:start + length]
            else:  # overlapping match: the last `offset` bytes repeat
                period = bytes(out[start:])
                out += (period * (length // offset + 1))[:length]
    except IndexError:
        raise ValueError('Corrupt LZ4 block: truncated length byte') from None
    if len(out) != size:
        raise ValueError('LZ4 block length mismatch: %d bytes, expected %d' % (len(out), size))
    return bytes(out)


def expand_packed(raw, allow_incomplete=False):
    """(SM64VM1 bytes, stats) for SM64VMZ1 input; (raw, None) for anything else.

    A truncated file raises ValueError unless allow_incomplete: then the bytes
    up to the last whole block (plus any raw-block prefix) come back, the same
    prefix a truncated SM64VM1 file would give, and stats['truncated'] says
    where it stopped.
    """
    if not is_packed(raw):
        return raw, None
    view = memoryview(raw)
    n = len(view)
    if n < PACK_HEADER.size + VM_HEADER.size:
        raise ValueError('Truncated SM64VMZ1 header')
    _, version, block_bytes = PACK_HEADER.unpack_from(view)
    if version != 1 or not 0 < block_bytes <= 16 * 1024 * 1024:
        raise ValueError('Unknown SM64VMZ1 version %d / block size %d' % (version, block_bytes))
    vm_magic, vm_version, header_bytes, word, region_bytes, *_, count, complete, scan_end, _devices = \
        VM_HEADER.unpack_from(view, PACK_HEADER.size)
    if vm_magic != b'SM64VM1\0' or (header_bytes, word, region_bytes) != (64, 4, 24):
        raise ValueError('SM64VMZ1 does not wrap an SM64VM1 header')
    offset = PACK_HEADER.size + VM_HEADER.size
    out = bytearray(view[PACK_HEADER.size:offset])
    zeros = bytes(block_bytes)
    stats = dict(format='SM64VMZ1', version=version, block_bytes=block_bytes, packed_bytes=n,
                 blocks={name: 0 for name in KIND_NAMES}, stored_bytes=0, zero_bytes=0,
                 excluded_bytes=0, excluded_ranges=[], regions=0, truncated=None)

    def truncated(kind, **where):
        if not allow_incomplete:
            raise ValueError('Truncated SM64VMZ1 (%s at file offset %d); pass allow_incomplete' % (kind, offset))
        stats['truncated'] = dict(kind=kind, file_offset=offset, **where)

    while offset < n and stats['truncated'] is None:
        if stats['regions'] == MAX_REGIONS:
            raise ValueError('Too many SM64VMZ1 regions')
        if n - offset < REGION.size:
            out += view[offset:]  # partial record, as in a truncated SM64VM1
            truncated('region_header')
            break
        base, length, _perm, _state, payload, _reason = REGION.unpack_from(view, offset)
        out += view[offset:offset + REGION.size]
        offset += REGION.size
        stats['regions'] += 1
        done = 0
        while done < payload:
            if n - offset < PACK_BLOCK.size:
                truncated('block_header', address=base + done)
                break
            address, logical, stored, kind = PACK_BLOCK.unpack_from(view, offset)
            if address != base + done or not 0 < logical <= min(block_bytes, payload - done):
                raise ValueError('SM64VMZ1 block out of place at 0x%08x' % (base + done))
            if kind > KIND_EXCLUDED or (kind in (KIND_ZERO, KIND_EXCLUDED) and stored) or \
                    (kind == KIND_RAW and stored != logical) or (kind == KIND_LZ4 and not 0 < stored < logical):
                raise ValueError('Invalid SM64VMZ1 block kind %d at 0x%08x' % (kind, address))
            data = view[offset + PACK_BLOCK.size:offset + PACK_BLOCK.size + stored]
            if len(data) < stored:
                if kind == KIND_RAW:
                    out += data  # the bytes that did reach the card
                truncated('block_data', address=address, declared=logical, captured=len(data) if kind == KIND_RAW else 0)
                break
            offset += PACK_BLOCK.size + stored
            if kind == KIND_RAW:
                out += data
            elif kind == KIND_LZ4:
                out += lz4_block(data, logical)
            else:
                out += zeros[:logical]
            stats['blocks'][KIND_NAMES[kind]] += 1
            stats['stored_bytes'] += stored
            if kind == KIND_ZERO:
                stats['zero_bytes'] += logical
            elif kind == KIND_EXCLUDED:
                stats['excluded_bytes'] += logical
                ranges = stats['excluded_ranges']
                if ranges and ranges[-1][0] + ranges[-1][1] == address:
                    ranges[-1][1] += logical
                else:
                    ranges.append([address, logical])
            done += logical
    if complete == 1 and stats['truncated'] is None and stats['regions'] != count:
        raise ValueError('SM64VMZ1 region count %d does not match the header (%d)' % (stats['regions'], count))
    stats['expanded_bytes'] = len(out)
    stats['excluded_ranges'] = [dict(address=hex(a), bytes=b) for a, b in stats['excluded_ranges']]
    return bytes(out), stats


def read_vm_bytes(path, allow_incomplete=False):
    """SM64VM1 bytes of a memory.vm, packed or not."""
    return expand_packed(Path(path).read_bytes(), allow_incomplete)[0]


def lz4_compressor(method='auto'):
    """A function bytes -> raw LZ4 block (or None), or None when unavailable.

    'auto' tries the python-lz4 module, then the lz4 command line tool.
    """
    if method == 'raw':
        return None
    try:
        import lz4.block

        def module(data):
            return lz4.block.compress(data, mode='default', store_size=False)
        return module
    except ImportError:
        pass
    tool = shutil.which('lz4')
    if tool:
        def command(data):
            # One frame with one block (-B7: 4 MiB maximum), no checksums.
            frame = subprocess.run([tool, '-1', '-B7', '-BI', '--no-frame-crc', '-c', '-q'], input=bytes(data),
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout
            if frame[:4] != b'\x04\x22\x4d\x18':
                raise ValueError('lz4 tool did not write an LZ4 frame')
            flags = frame[4]
            at = 7 + (8 if flags & 8 else 0) + (4 if flags & 1 else 0)
            word = struct.unpack_from('<I', frame, at)[0]
            if word & 0x80000000:
                return None  # stored uncompressed: the packer keeps it raw
            block = frame[at + 4:at + 4 + word]
            if struct.unpack_from('<I', frame, at + 4 + word)[0] != 0:
                raise ValueError('lz4 tool wrote more than one block')
            return block
        return command
    if method == 'lz4':
        raise RuntimeError('LZ4 needs the python lz4 module or the lz4 command line tool')
    return None


def pack(raw, compress=None, block_bytes=BLOCK_BYTES):
    """SM64VMZ1 bytes for one SM64VM1 file, split into blocks like the console.

    compress(bytes) returns one raw LZ4 block or None; None stores raw blocks.
    Lossless: expand_packed(pack(raw))[0] == raw.
    """
    if is_packed(raw):
        raise ValueError('Already SM64VMZ1')
    view = memoryview(raw)
    vm_magic, _version, header_bytes, word, region_bytes, *_ = VM_HEADER.unpack_from(view)
    if vm_magic != b'SM64VM1\0' or (header_bytes, word, region_bytes) != (64, 4, 24):
        raise ValueError('Not an SM64VM1 file')
    parts = [PACK_HEADER.pack(MAGIC, 1, block_bytes), bytes(view[:VM_HEADER.size])]
    zeros = bytes(block_bytes)
    offset = VM_HEADER.size
    while offset < len(view):
        if len(view) - offset < REGION.size:
            raise ValueError('Truncated SM64VM1 region record at %d' % offset)
        base, _length, _perm, _state, payload, _reason = REGION.unpack_from(view, offset)
        parts.append(bytes(view[offset:offset + REGION.size]))
        offset += REGION.size
        if len(view) - offset < payload:
            raise ValueError('Truncated SM64VM1 payload at 0x%08x' % base)
        for done in range(0, payload, block_bytes):
            logical = min(block_bytes, payload - done)
            chunk = view[offset + done:offset + done + logical]
            if chunk == zeros[:logical]:
                parts.append(PACK_BLOCK.pack(base + done, logical, 0, KIND_ZERO))
                continue
            block = compress(chunk) if compress else None
            if block and len(block) < logical:
                parts += [PACK_BLOCK.pack(base + done, logical, len(block), KIND_LZ4), block]
            else:
                parts += [PACK_BLOCK.pack(base + done, logical, logical, KIND_RAW), bytes(chunk)]
        offset += payload
    return b''.join(parts)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    expand = commands.add_parser('expand', help='write the SM64VM1 bytes of a memory.vm')
    expand.add_argument('input', type=Path)
    expand.add_argument('output', type=Path)
    expand.add_argument('--allow-incomplete', action='store_true', help='expand the readable prefix of a truncated file')
    info = commands.add_parser('info', help='print SM64VMZ1 statistics as JSON')
    info.add_argument('input', type=Path)
    packer = commands.add_parser('pack', help='pack an SM64VM1 file (lossless, checked by expanding it again)')
    packer.add_argument('input', type=Path)
    packer.add_argument('output', type=Path)
    packer.add_argument('--method', choices=('auto', 'lz4', 'raw'), default='auto',
                        help='auto: LZ4 when python-lz4 or the lz4 tool is present, else raw blocks')
    args = parser.parse_args()
    raw = args.input.read_bytes()
    if args.command == 'pack':
        compress = lz4_compressor(args.method)
        packed = pack(raw, compress)
        expanded, stats = expand_packed(packed)
        if expanded != raw:
            raise SystemExit('Packed file does not expand to the input; nothing written')
        args.output.write_bytes(packed)
        stats.update(input_sha256=hashlib.sha256(raw).hexdigest(), compressor=compress is not None)
    else:
        expanded, stats = expand_packed(raw, getattr(args, 'allow_incomplete', False))
        if stats is None:
            stats = dict(format='SM64VM1', bytes=len(raw))
        if args.command == 'expand':
            args.output.write_bytes(expanded)
        stats['VM_sha256'] = hashlib.sha256(expanded).hexdigest()
    print(json.dumps(stats, indent=2))


if __name__ == '__main__':
    main()
