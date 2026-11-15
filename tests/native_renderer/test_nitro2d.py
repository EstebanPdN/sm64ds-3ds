"""Authored original-menu resource contracts; no ROM/assets in these tests."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from native_renderer.nitro2d import SourceError, blocks, characters, palette, rgba, cells


def container(magic, version, records):
    body = b''.join(tag + struct.pack('<I', 8 + len(data)) + data for tag, data in records)
    return struct.pack('<4sHHIHH', magic, 0xfeff, version, 16 + len(body), 16, len(records)) + body


class Nitro2D(unittest.TestCase):
    def test_tile_order_nibbles_palette_and_transparency(self):
        pixels = bytes([0x21, 0x40, 0x65, 0x87] * 8 + [0x99] * 32)
        raw = container(b'NCCG', 0x100, [(b'CHAR', struct.pack('<3I', 2, 1, 0) + pixels)])
        source = characters(raw)
        self.assertEqual((source.width, source.height, source.eight_bit), (16, 8, False))
        self.assertEqual([source.index(x, 0) for x in range(16)], [1, 2, 0, 4, 5, 6, 7, 8] + [9]*8)
        colours = bytearray(512)
        for index, word in [(0, 0x421), (1, 0x001f), (2, 0x03e0), (9, 0x7c00)]:
            struct.pack_into('<H', colours, (5*16+index)*2, word)
        colour_resource = container(b'NCCL', 0x100, [(b'PALT', struct.pack('<2I', 16, 16) + colours)])
        data = rgba(source, palette(colour_resource), 5)
        self.assertEqual(data[:12], bytes([255, 0, 0, 255, 0, 255, 0, 255, 8, 8, 8, 0]))
        self.assertEqual(data[8*4:9*4], bytes([0, 0, 255, 255]))
        with self.assertRaises(SourceError): source.index(16, 0)
        with self.assertRaises(SourceError): rgba(source, colours, 16)

    def test_eight_bit_tiles_ignore_palette_bank(self):
        source = characters(container(b'NCCG', 0x100, [(b'CHAR', struct.pack('<3I', 1, 1, 1) + bytes(range(64)))]))
        self.assertEqual(source.index(7, 6), 55)
        colours = struct.pack('<256H', *range(256))
        self.assertEqual(rgba(source, colours, 0), rgba(source, colours, 15))

    def test_native_cell_metadata_retains_disabled_and_affine_modes(self):
        record = struct.pack('<hh12BHH', -9, -7, 0, 7, 1, 0, 3, 1, 1, 0, 1, 2, 2, 14, 73, 0)
        hidden = bytearray(record);hidden[8] = 2
        payload = struct.pack('<II', 1, 2) + record + hidden
        result = cells(container(b'NCOB', 0x102, [(b'CELL', payload)]))
        a, b = result[0]
        self.assertEqual((a.x, a.y, a.width, a.height, a.tile, a.palette, a.priority), (-9, -7, 32, 16, 73, 14, 2))
        self.assertTrue(a.affine and a.double_size and a.flip_x and a.mosaic)
        self.assertEqual(a.affine_index, 7)
        self.assertTrue(b.hidden and not b.affine)

    def test_container_and_semantic_bounds(self):
        valid = container(b'NCCG', 0x100, [(b'CHAR', struct.pack('<3I', 1, 1, 0) + bytes(32))])
        bad = [valid[:-1], valid+b'\0', valid[:4]+b'\0\0'+valid[6:],
               container(b'NCCG', 0x100, [(b'CHAR', struct.pack('<3I', 129, 1, 0))]),
               container(b'NCCG', 0x100, [(b'CHAR', struct.pack('<3I', 1, 1, 2)+bytes(32))]),
               container(b'NCCG', 0x100, [(b'NONE', bytes(12))]),
               container(b'NCCG', 0x100, [(b'CHAR', bytes(12)), (b'CHAR', bytes(12))])]
        for data in bad:
            with self.subTest(data=data[:24]), self.assertRaises(SourceError): characters(data)
        with self.assertRaises(SourceError): cells(container(b'NCOB', 0x102, [(b'CELL', struct.pack('<I', 129))]))
        with self.assertRaises(SourceError): palette(container(b'NCCL', 0x100, [(b'PALT', struct.pack('<2I', 16, 17)+bytes(512))]))


if __name__ == '__main__':
    unittest.main()
