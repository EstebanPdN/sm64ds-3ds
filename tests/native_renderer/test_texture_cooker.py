"""Independent differential checks against the unchanged provider decoder."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from native_renderer.cook_model import texture_rgba
from native_renderer.audit_models import SourceError


def source(fmt,w,h,main,index,palette,transparent):
    output=bytearray(128)+main+index+palette
    for at,value in ((20,1),(24,64),(28,1),(32,84),(68,128),(72,len(main)),
                     (80,(fmt<<26)|((w.bit_length()-4)<<20)|((h.bit_length()-4)<<23)|(int(transparent)<<29)),
                     (88,128+len(main)+len(index)),(92,len(palette))):
        struct.pack_into('<I',output,at,value)
    return output


class TextureCookerTests(unittest.TestCase):
    def compare(self,fmt,w,h,main,index,palette,transparent=False):
        data=source(fmt,w,h,main,index,palette,transparent)
        actual_w,actual_h,actual=texture_rgba(data,0,0)
        self.assertEqual((actual_w,actual_h),(w,h))
        request=b'NTEXOR01'+struct.pack('<7I',fmt,w,h,int(transparent),len(main),len(index),len(palette))+main+index+palette
        with tempfile.TemporaryDirectory() as directory:
            a=Path(directory)/'source';b=Path(directory)/'rgba'
            a.write_bytes(request);subprocess.run([ORACLE,str(a),str(b)],check=True)
            self.assertEqual(actual,b.read_bytes())

    def test_all_direct_colour_values(self):
        main=b''.join(struct.pack('<H',i|0x8000) for i in range(32768))
        self.compare(7,256,128,main,b'',b'')
        self.compare(7,8,8,bytes(128),b'',b'')

    def test_compressed_modes(self):
        self.compare(5,8,8,struct.pack('<4I',*([0xe4e4e4e4]*4)),
            struct.pack('<4H',*(mode<<14 for mode in range(4))),struct.pack('<4H',31,31<<5,31<<10,32767))

    def test_indexed_formats(self):
        for fmt,w,h,bits,pal_size in ((1,16,16,8,32),(2,8,8,2,4),(3,8,8,4,16),
                                      (4,16,16,8,256),(6,16,16,8,8)):
            with self.subTest(fmt=fmt):
                palette=b''.join(struct.pack('<H',(i*1031)&32767) for i in range(pal_size))
                main=bytes(i&255 for i in range(w*h*bits//8))
                self.compare(fmt,w,h,main,b'',palette,fmt in (2,4))

    def test_sprite_colour_zero_variants(self):
        for fmt,bits,size in [(2,2,4),(3,4,16),(4,8,256)]:
            palette=b''.join(struct.pack('<H',i&32767) for i in range(size))
            for original in (False,True):
                data=source(fmt,8,8,bytes(64*bits//8),b'',palette,original);before=bytes(data)
                for override in (False,True):
                    w,h,rgba=texture_rgba(data,0,0,override)
                    self.assertEqual((w,h),(8,8));self.assertEqual(rgba[3::4],bytes([0 if override else 255])*64)
                retained=texture_rgba(data,0,0,True,True)[2]
                # Source palette zero is black here. A coloured zero proves
                # that disabling the mask can restore its RGB without a copy.
                coloured=bytearray(data);struct.pack_into('<H',coloured,128+64*bits//8,31)
                opaque=texture_rgba(coloured,0,0,False)[2]
                masked=texture_rgba(coloured,0,0,True,True)[2]
                self.assertEqual(masked[:3],opaque[:3]);self.assertEqual(masked[3],0)
                self.assertEqual(retained[3::4],bytes(64))
                self.assertEqual(data,before)
        data=source(6,8,8,bytes(64),b'',bytes(16),False)
        with self.assertRaises(SourceError):texture_rgba(data,0,0,True)

    def test_compressed_palette_offsets_and_colours(self):
        import random
        rng=random.Random(0x534d3634)
        palette=b''.join(struct.pack('<H',rng.randrange(32768)) for _ in range(64))
        for _ in range(12):
            main=b''.join(struct.pack('<I',rng.getrandbits(32)) for _ in range(8))
            descriptor=b''.join(struct.pack('<H',rng.randrange(30)|(rng.randrange(4)<<14)) for _ in range(8))
            self.compare(5,16,8,main,descriptor,palette)

    def test_truncated_or_out_of_range_sources(self):
        data=source(5,8,8,bytes(16),struct.pack('<4H',4,0,0,0),bytes(8),False)
        with self.assertRaises(SourceError):texture_rgba(data,0,0)
        with self.assertRaises(SourceError):texture_rgba(data[:-1],0,0)


if __name__=='__main__':
    ORACLE=sys.argv.pop(1)
    unittest.main()
