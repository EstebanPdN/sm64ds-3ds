"""ROM-free independent reconstruction of every prepared mask texel."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from native_renderer.font import prepare_mask
from native_renderer.audit_models import SourceError


class FontTests(unittest.TestCase):
    def test_mask_all_codes_rows_and_native_layout(self):
        original=bytes(0xf0 if i%3 else 0x0f for i in range(16384))
        tiled,mask=prepare_mask(original)
        self.assertEqual(len(tiled),16384)
        self.assertEqual((tiled,mask),prepare_mask(original))
        for code in range(256):
            for y in range(16):
                for x in range(8):
                    # Original ARM src increments32words at the glyph's
                    # bottom half; this oracle uses those row addresses.
                    at=(code//32*64+code%32)*32+y%8*4+(1024 if y>=8 else 0)+x//2
                    expected=(original[at]>>(4*(x%2)))&15
                    px=code%32*8+x;py=code//32*16+y;gy=127-py
                    morton=0
                    for bit in range(3):morton|=((px>>bit)&1)<<(bit*2);morton|=((gy>>bit)&1)<<(bit*2+1)
                    out=(gy//8*32+px//8)*64+morton
                    self.assertEqual((tiled[out//2]>>((out%2)*4))&15,expected)
                    self.assertEqual(mask[py*256+px],expected)

    def test_incompatible_font_rejected(self):
        for data in [bytes(16383),bytes(16385),bytes([0x70])+bytes(16383)]:
            with self.assertRaises(SourceError):prepare_mask(data)


if __name__=='__main__':unittest.main()
