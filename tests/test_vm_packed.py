#!/usr/bin/env python3
"""SM64VMZ1 (packed memory.vm) decoding with hand-made blocks; no console data.

Zero, raw, LZ4 and excluded blocks expand to the exact SM64VM1 bytes; the LZ4
blocks are built by hand (overlapping matches, 255-byte length extensions), so
no LZ4 library is needed. Every .vm reader must see the same bytes for both
formats. A reference-encoder round trip runs only when python-lz4 or the lz4
tool is installed.
"""
from pathlib import Path
import hashlib
import struct
import subprocess
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
import vm_packed  # noqa: E402
from vm_packed import (MAGIC, PACK_BLOCK, PACK_HEADER, REGION, VM_HEADER, KIND_EXCLUDED, KIND_LZ4, KIND_RAW,  # noqa: E402
                       KIND_ZERO, expand_packed, lz4_block, pack)
from inspect_integrated_vm import VM  # noqa: E402
from read_mapped_capture import MappedCapture  # noqa: E402

# Three sequences: an overlapping match (offset 3 < length 10), 20 literals
# (one extension byte) with a 277-byte offset-1 run (extensions 255, 3), and
# the final literals-only sequence.
LZ4_HANDMADE = (bytes([0x36]) + b'abc' + b'\x03\x00' +
                bytes([0xFF, 5]) + bytes(range(100, 120)) + b'\x01\x00' + bytes([255, 3]) +
                bytes([0x60]) + b'tail!!')
LZ4_EXPECTED = b'abc' + b'abcabcabca' + bytes(range(100, 120)) + bytes([119]) * 277 + b'tail!!'

DATA = 0x1000  # first payload address of the synthetic process


def vm_header(count, complete=1, version=2):
    return VM_HEADER.pack(b'SM64VM1\0', version, 64, 4, 24, 10, 20, 3, 7, count, complete, 0x40000000, 0)


def raw_payload():
    return bytes((i * 7 + 3) & 255 for i in range(4096))


def synthetic():
    """(SM64VM1 bytes, SM64VMZ1 bytes) of one process: unreadable, data, unreadable."""
    blocks = [(4096, KIND_ZERO, b''), (4096, KIND_RAW, raw_payload()),
              (len(LZ4_EXPECTED), KIND_LZ4, LZ4_HANDMADE), (8192, KIND_EXCLUDED, b'')]
    size = sum(logical for logical, _, _ in blocks)
    regions = [REGION.pack(0, DATA, 0, 0, 0, 1), REGION.pack(DATA, size, 3, 5, size, 0),
               REGION.pack(DATA + size, 0x40000000 - DATA - size, 0, 0, 0, 1)]
    payload = bytes(4096) + raw_payload() + LZ4_EXPECTED + bytes(8192)
    plain = vm_header(3) + regions[0] + regions[1] + payload + regions[2]
    packed = [PACK_HEADER.pack(MAGIC, 1, 512 * 1024), vm_header(3), regions[0], regions[1]]
    address = DATA
    for logical, kind, stored in blocks:
        packed += [PACK_BLOCK.pack(address, logical, len(stored), kind), stored]
        address += logical
    packed.append(regions[2])
    return plain, b''.join(packed)


class Lz4Block(unittest.TestCase):
    def test_overlapping_matches_and_length_extensions(self):
        self.assertEqual(lz4_block(LZ4_HANDMADE, len(LZ4_EXPECTED)), LZ4_EXPECTED)
        # Offset 2, length 5: the copy reads bytes it is writing.
        self.assertEqual(lz4_block(bytes([0x21]) + b'xy' + b'\x02\x00' + bytes([0x50]) + b'12345', 12), b'xyxyxyx12345')
        # 270 literals: extension bytes 255 then 0.
        literals = bytes(range(256)) + bytes(range(14))
        self.assertEqual(lz4_block(bytes([0xF0, 255, 0]) + literals, 270), literals)

    def test_corrupt_blocks_are_rejected(self):
        for block, size in [(bytes([0x10]) + b'a' + b'\x00\x00' + bytes([0x10]) + b'b', 6),   # offset 0
                            (bytes([0x10]) + b'a' + b'\x02\x00' + bytes([0x10]) + b'b', 6),   # before the output
                            (LZ4_HANDMADE, len(LZ4_EXPECTED) - 1),                           # longer than declared
                            (LZ4_HANDMADE, len(LZ4_EXPECTED) + 1),                           # shorter than declared
                            (bytes([0xF0, 255]), 300),                                       # truncated extension
                            (bytes([0x50]) + b'abc', 5),                                     # literals past the end
                            (bytes([0x10]) + b'a' + b'\x01', 5)]:                            # truncated offset
            with self.assertRaises(ValueError):
                lz4_block(block, size)


class Expand(unittest.TestCase):
    def test_every_block_kind_expands_to_the_sm64vm1_bytes(self):
        plain, packed = synthetic()
        expanded, stats = expand_packed(packed)
        self.assertEqual(expanded, plain)
        self.assertEqual(stats['blocks'], dict(zero=1, raw=1, lz4=1, excluded=1))
        self.assertEqual(stats['zero_bytes'], 4096)
        self.assertEqual(stats['stored_bytes'], 4096 + len(LZ4_HANDMADE))
        self.assertEqual(stats['excluded_ranges'], [dict(address=hex(DATA + 4096 * 2 + len(LZ4_EXPECTED)), bytes=8192)])
        self.assertEqual(expand_packed(plain), (plain, None))  # SM64VM1 passes through

    def test_readers_see_the_same_bytes_for_both_formats(self):
        plain, packed = synthetic()
        with tempfile.TemporaryDirectory() as folder:
            a = Path(folder) / 'plain.vm'
            b = Path(folder) / 'packed.vm'
            a.write_bytes(plain)
            b.write_bytes(packed)
            vm_a, vm_b = VM(a), VM(b)
            self.assertEqual(vm_a.raw, vm_b.raw)
            self.assertIsNone(vm_a.packed)
            self.assertEqual(vm_b.packed['format'], 'SM64VMZ1')
            self.assertEqual(vm_b.read(DATA + 4096, 8), raw_payload()[:8])
            self.assertEqual(vm_b.read(DATA + 8192 + 13, 20), bytes(range(100, 120)))
            map_a, map_b = MappedCapture(a), MappedCapture(b)
            self.assertEqual(map_a.raw, map_b.raw)
            self.assertEqual(map_b.read(DATA + 8192, 16), LZ4_EXPECTED[:16])
            # Both command lines write the SM64VM1 bytes back.
            for command in ([str(TOOLS / 'vm_packed.py'), 'expand', str(b), str(Path(folder) / 'one.vm')],
                            [str(TOOLS / 'inspect_integrated_vm.py'), str(b), '--expand', str(Path(folder) / 'two.vm')]):
                subprocess.run([sys.executable, *command], check=True, capture_output=True)
            for name in ('one.vm', 'two.vm'):
                self.assertEqual(hashlib.sha256((Path(folder) / name).read_bytes()).digest(), hashlib.sha256(plain).digest())

    def test_truncated_file_needs_explicit_prefix_analysis(self):
        plain, packed = synthetic()
        incomplete = packed[:PACK_HEADER.size] + vm_header(0, complete=0) + packed[PACK_HEADER.size + VM_HEADER.size:]
        # Cut 100 bytes into the raw block's data.
        cut = PACK_HEADER.size + VM_HEADER.size + 2 * REGION.size + 2 * PACK_BLOCK.size + 100
        with self.assertRaises(ValueError):
            expand_packed(incomplete[:cut])
        prefix, stats = expand_packed(incomplete[:cut], allow_incomplete=True)
        self.assertEqual(stats['truncated']['kind'], 'block_data')
        self.assertEqual(stats['truncated']['captured'], 100)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'cut.vm'
            path.write_bytes(incomplete[:cut])
            with self.assertRaises(ValueError):
                MappedCapture(path)
            capture = MappedCapture(path, allow_incomplete=True)
            self.assertFalse(capture.complete)
            self.assertEqual(capture.read(DATA, 4096 + 100), bytes(4096) + raw_payload()[:100])
            with self.assertRaises(ValueError):
                capture.read(DATA + 4096 + 99, 2)
        # A cut inside a block header keeps every earlier block.
        prefix, stats = expand_packed(incomplete[:cut - 100 - 8], allow_incomplete=True)
        self.assertEqual(stats['truncated']['kind'], 'block_header')
        self.assertEqual(prefix, vm_header(0, complete=0) + plain[VM_HEADER.size:VM_HEADER.size + 2 * REGION.size + 4096])

    def test_malformed_blocks_are_rejected(self):
        _, packed = synthetic()
        first = PACK_HEADER.size + VM_HEADER.size + 2 * REGION.size
        for field, value in [(0, DATA + 4), (1, 0), (2, 16), (3, 9)]:  # address, logical, stored, kind
            words = list(PACK_BLOCK.unpack_from(packed, first))
            words[field] = value
            broken = packed[:first] + PACK_BLOCK.pack(*words) + packed[first + PACK_BLOCK.size:]
            with self.assertRaises(ValueError):
                expand_packed(broken)


class Pack(unittest.TestCase):
    def process(self):
        """An SM64VM1 process whose data region spans three 512 KiB blocks."""
        size = 512 * 1024 * 2 + 8192
        data = bytearray(size)
        data[:4096] = raw_payload()
        data[512 * 1024 + 100:512 * 1024 + 4196] = bytes(range(256)) * 16  # compressible
        regions = [REGION.pack(0, DATA, 0, 0, 0, 1), REGION.pack(DATA, size, 3, 5, size, 0),
                   REGION.pack(DATA + size, 0x40000000 - DATA - size, 0, 0, 0, 2)]
        return vm_header(3) + regions[0] + regions[1] + bytes(data) + regions[2]

    def test_raw_blocks_round_trip(self):
        plain = self.process()
        packed = pack(plain)
        expanded, stats = expand_packed(packed)
        self.assertEqual(expanded, plain)
        self.assertEqual(stats['blocks'], dict(zero=1, raw=2, lz4=0, excluded=0))
        self.assertLess(len(packed), len(plain))

    def test_reference_encoder_round_trip(self):
        compress = vm_packed.lz4_compressor('auto')
        if compress is None:
            self.skipTest('neither python-lz4 nor the lz4 tool is installed')
        plain = self.process()
        packed = pack(plain, compress)
        expanded, stats = expand_packed(packed)
        self.assertEqual(expanded, plain)
        self.assertGreaterEqual(stats['blocks']['lz4'], 2)
        # The pure-Python decoder agrees with the reference encoder.
        sample = bytes((i * i) & 255 for i in range(70000)) + bytes(30000) + b'end of sample'
        self.assertEqual(lz4_block(compress(sample), len(sample)), sample)


if __name__ == '__main__':
    unittest.main()
