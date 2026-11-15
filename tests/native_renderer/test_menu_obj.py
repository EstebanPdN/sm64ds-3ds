"""Semantic menu asset splices: independent expected source ranges/colours."""
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from native_renderer.menu_obj import obj_view
from native_renderer.menu2d import bake_cell
from native_renderer.nitro2d import Characters, CellPart


class MenuObj(unittest.TestCase):
    def setUp(self):
        self.title = Characters(256, 128, False, bytes([0x11]) * 16384)
        self.file = Characters(256, 128, False, bytes([0x22]) * 16384)
        self.colours = b''.join(struct.pack('<H', i) for i in range(256))
        self.icons = bytes([0x33]) * 8192
        self.icon_colours = b''.join(struct.pack('<H', 512 + i) for i in range(112))
        self.replacement = Characters(256, 80, False, bytes([0x44]) * 10240)

    def view(self, layout, engine, localized=False):
        return obj_view(self.title, self.file, self.colours, self.icons, self.icon_colours,
                        self.replacement if localized else None, layout, engine)

    def test_main_title_half_and_icons_use_distinct_sources(self):
        source, colours = self.view(1, 0)
        self.assertEqual(source.pixels, bytes([0x11]) * 8192 + bytes([0x33]) * 8192)
        self.assertEqual(colours[:480], self.colours[:480])
        self.assertEqual(struct.unpack('<16H', colours[480:]), tuple(range(608, 624)))

    def test_sub_language_keeps_uncopied_default_suffix(self):
        source, colours = self.view(1, 1, True)
        self.assertEqual(source.pixels, bytes([0x44]) * 10240 + bytes([0x11]) * 6144)
        self.assertEqual(colours, self.colours)
        source, _ = self.view(3, 1, True)
        self.assertEqual(source.pixels, bytes([0x44]) * 10240 + bytes([0x22]) * 6144)

    def test_file_layout_preserves_main_title_data_and_icon_tail(self):
        source, _ = self.view(3, 0)
        self.assertEqual(source.pixels[:8192], bytes([0x11]) * 8192)
        source, _ = self.view(3, 0, True)
        self.assertEqual(source.pixels, bytes([0x44]) * 8192 + bytes([0x33]) * 8192)
        source, _ = self.view(3, 1)
        self.assertEqual(source.pixels, bytes([0x22]) * 16384)

    def test_baked_icon_uses_patched_palette_without_affecting_sub(self):
        part = CellPart(0, 0, 8, 8, 256, 15, 0, False, False, 0, False, False, False, 0, False, False)
        source, colours = self.view(1, 0)
        rgba, _ = bake_cell(source, colours, [part])
        word = 611  # icon nibble3, palette bank15 copied from external entry99
        rgb = [((word >> s) & 31) for s in (0, 5, 10)]
        self.assertEqual(rgba[:4], bytes([(v << 3) | (v >> 3) for v in rgb] + [255]))
        source, colours = self.view(1, 1)
        rgba, _ = bake_cell(source, colours, [part])
        word = 241  # unchanged SUB nibble1 in palette15
        rgb = [((word >> s) & 31) for s in (0, 5, 10)]
        self.assertEqual(rgba[:4], bytes([(v << 3) | (v >> 3) for v in rgb] + [255]))


if __name__ == '__main__':
    unittest.main()
