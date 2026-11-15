"""Native image-bank cross-language and malformed-writer checks."""
from pathlib import Path
import hashlib
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from native_renderer.bank import Image
from native_renderer.image_bank import Entry,build


class ImageBankTests(unittest.TestCase):
    def test_cross_language(self):
        entries=[Entry(Image(8,16,bytes([25])*512),True),Entry(Image(16,8,bytes([61])*512))]
        sha=hashlib.sha256(b'authored images').digest()
        raw=build(entries,sha)
        self.assertEqual(raw,build(entries,sha))
        self.assertEqual(raw[:8],b'SM64NIM1')
        self.assertEqual(struct.unpack_from('<5I',raw,8),(1,len(raw),2,128,1024))
        self.assertEqual(raw[32:64],sha)
        checksum=struct.unpack_from('<I',raw,28)[0]
        self.assertEqual(zlib.crc32(raw[:28]+bytes(4)+raw[32:]),checksum)
        self.assertEqual(struct.unpack_from('<5I',raw,128),(256,512,8,16,1))
        self.assertEqual(struct.unpack_from('<5I',raw,160),(768,512,16,8,0))
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'fixture.nim';path.write_bytes(raw)
            subprocess.run([PARSER,str(path)],check=True)

    def test_counts_and_image_contract(self):
        good=Entry(Image(8,8,bytes(256)))
        for entries,sha in [([],bytes(32)),([good]*129,bytes(32)),([good],bytes(31)),
                            ([Entry(Image(4,8,bytes(128)))],bytes(32)),
                            ([Entry(Image(8,8,bytes(255)))],bytes(32)),
                            ([Entry(good.image,1)],bytes(32))]:
            with self.subTest(count=len(entries)):
                with self.assertRaises(ValueError):build(entries,sha)

    def test_native_alpha4_mixed_bank(self):
        entries=[Entry(Image(8,8,bytes([0xf1])*32),texture_format=1),Entry(Image(8,8,bytes([127])*256))]
        raw=build(entries,bytes(32))
        self.assertEqual(raw[:8],b'SM64NIM2')
        self.assertEqual(struct.unpack_from('<5I',raw,8),(2,640,2,128,288))
        self.assertEqual(struct.unpack_from('<8I',raw,128),(256,32,8,8,0,1,0,0))
        self.assertEqual(raw[288:384],bytes(96))
        self.assertEqual(struct.unpack_from('<2I',raw,160),(384,256))
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'mixed.nim';path.write_bytes(raw);subprocess.run([PARSER,str(path)],check=True)
        for entry in [Entry(Image(8,8,bytes(32)),texture_format=2),
                      Entry(Image(8,8,bytes(32)),texture_format=True),
                      Entry(Image(8,8,bytes(33)),texture_format=1)]:
            with self.assertRaises(ValueError):build([entry],bytes(32))


if __name__=='__main__':
    PARSER=sys.argv.pop(1)
    unittest.main()
