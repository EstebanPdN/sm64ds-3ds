import subprocess
import struct
import zlib
import sys
from pathlib import Path
import tempfile
import unittest
from dataclasses import replace
from make_model_fixture import fixture
from native_renderer.bank import Mesh, Image, Material, Part, build


class WriterTests(unittest.TestCase):
    def test_normal_layout_cross_language_and_refusal(self):
        vertices=b''.join(struct.pack('<5f4B4h',0,0,0,0,0,255,255,255,255,-512,511,0,i%2) for i in range(3))
        mesh=Mesh(vertices,b'\0\0\1\0\2\0',True)
        bank=build([mesh],[],[Material()],[Part(0,0,0,0,3)],1,bytes(32))
        self.assertEqual(bank[:8],b'SM64NMD3')
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'normal.nmd';p.write_bytes(bank);subprocess.run([READER,str(p)],check=True)
            for at,value in ((8,2),(128+16,2),(256+24,512)):
                broken=bytearray(bank)
                if at==8:broken[7]=ord('2')
                struct.pack_into('<I' if at<256 else '<h',broken,at,value)
                struct.pack_into('<I',broken,52,0);struct.pack_into('<I',broken,52,zlib.crc32(broken))
                p.write_bytes(broken)
                self.assertNotEqual(subprocess.run([READER,str(p)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode,0)

    def test_native_reader(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'authored.nmd';path.write_bytes(fixture())
            subprocess.run([READER,str(path)],check=True)

    def test_invalid_references(self):
        mesh=Mesh(bytes(72),b'\x00\x00\x01\x00\x02\x00')
        image=Image(8,8,bytes(256));m=Material(texture=0);p=Part(0,0,0,0,3)
        for bad in (replace(p,matrix=1),replace(p,material=1),replace(p,count=6),replace(p,first=1)):
            with self.assertRaises(ValueError):build([mesh],[image],[m],[bad],1,bytes(32))
        for bad in (replace(m,texture=1),replace(m,uv=(1,0,0,0,float('nan'),0)),replace(m,cull=9),replace(m,wrap_s=3)):
            with self.assertRaises(ValueError):build([mesh],[image],[bad],[p],1,bytes(32))


if __name__=='__main__':
    READER=sys.argv.pop(1)
    unittest.main()
