"""Independent cell bake expectations: priority, masks, flips and colour policy."""
from dataclasses import replace
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from native_renderer.menu2d import bake_cell, SourceError
from native_renderer.nitro2d import CellPart, Characters


class Menu2D(unittest.TestCase):
    def setUp(self):
        self.pixels=bytearray(32*32)
        self.pixels[0]=1
        self.pixels[32:64]=bytes([0x22]*32)
        self.source=Characters(256,8,False,bytes(self.pixels))
        self.colours=bytearray(512)
        struct.pack_into('<H',self.colours,2,31)
        struct.pack_into('<H',self.colours,4,31<<5)
        struct.pack_into('<H',self.colours,(5*16+1)*2,31<<10)
        self.part=CellPart(-8,-4,8,8,0,0,0,False,False,0,False,False,False,0,False,False)

    def test_reference_colour_transparency_and_native_bounds(self):
        data,meta=bake_cell(self.source,self.colours,[self.part])
        self.assertEqual((meta['left'],meta['top'],meta['width'],meta['height']),(-8,-4,8,8))
        self.assertEqual(data[:8],bytes([251,0,0,255,0,0,0,0]))
        self.assertEqual(sum(value!=0 for value in data[4:]),0)
        normal,_=bake_cell(self.source,self.colours,[self.part],False)
        self.assertEqual(normal[:4],bytes([255,0,0,255]))

    def test_lower_original_object_index_wins_overlap(self):
        green=replace(self.part,tile=1)
        data,_=bake_cell(self.source,self.colours,[self.part,green])
        self.assertEqual(data[:8],bytes([251,0,0,255,0,251,0,255]))

    def test_source_flip_and_palette_are_baked_once(self):
        part=replace(self.part,flip_x=True,palette=5)
        data,_=bake_cell(self.source,self.colours,[part])
        self.assertEqual(data[7*4:8*4],bytes([0,0,251,255]))
        self.assertEqual(data[:4],bytes(4))

    def test_unknown_runtime_policies_are_not_flattened(self):
        for part in [replace(self.part,affine=True),replace(self.part,hidden=True),replace(self.part,mode=1),
                     replace(self.part,mosaic=True),replace(self.part,eight_bit=True),replace(self.part,x=512),replace(self.part,tile=64)]:
            with self.subTest(part=part), self.assertRaises(SourceError): bake_cell(self.source,self.colours,[part])
        with self.assertRaises(SourceError): bake_cell(self.source,self.colours,[self.part,replace(self.part,priority=1)])


if __name__=='__main__':unittest.main()
