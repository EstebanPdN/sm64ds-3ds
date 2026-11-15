"""Particle image preparation: bounded parsing and an independent pixel oracle.

Synthetic contracts run without a ROM. Supplying the owner's supported ROM
also checks every reviewed core/menu texture against the unchanged decoder.
"""
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from native_renderer.audit_models import SourceError
from native_renderer.particles import parse,prepare,reviewed_sources
from native_renderer.textures import decode_texture


def resource(fmt=2):
    width,height=8,8
    sizes={1:(64,64),2:(16,8),3:(32,32),4:(64,512),6:(64,16),7:(128,0)}
    pixels,palette=sizes[fmt]
    texture=struct.pack('<8I',0x53505420,fmt,pixels,32+pixels,palette,32+pixels+palette,0,32+pixels+palette)
    texture+=bytes(pixels+palette)
    return bytearray(b' APS10_1'+struct.pack('<HH5I',1,1,0,56,len(texture),88,0)+bytes(56)+texture)


def oracle_pixels(texture,zero):
    request=b'NTEXOR01'+struct.pack('<7I',texture.fmt,texture.width,texture.height,int(zero),
        len(texture.pixels),0,len(texture.palette))+texture.pixels+texture.palette
    with tempfile.TemporaryDirectory() as directory:
        a=Path(directory)/'source';b=Path(directory)/'rgba'
        a.write_bytes(request);subprocess.run([ORACLE,str(a),str(b)],check=True)
        return b.read_bytes()


class ParticleImagesTests(unittest.TestCase):
    def test_all_uncompressed_formats_and_native_parser(self):
        for fmt in (1,2,3,4,6,7):
            with self.subTest(fmt=fmt):
                raw=resource(fmt);before=bytes(raw);defs,textures=parse(raw)
                self.assertEqual(defs,(0,));self.assertEqual(textures[0].fmt,fmt)
                bank,report=prepare(raw,'authored')
                self.assertEqual(raw,before);self.assertEqual(report['images'],1)
                self.assertEqual(bank,prepare(raw,'authored')[0])
                self.assertEqual(report['gpu_bytes'],256)
                with tempfile.TemporaryDirectory() as directory:
                    path=Path(directory)/'test.nim';path.write_bytes(bank)
                    subprocess.run([PARSER,str(path)],check=True,stdout=subprocess.DEVNULL)

    def test_bad_headers_offsets_counts_formats(self):
        raw=resource()
        edits=[(0,0),(8,0),(8,0x00810001),(12,1),(16,57),(20,0),(24,89),(28,1),
               (32,1<<8),(88,0),(92,5),(92,0x82),(96,15),(100,0),(104,7),(116,0),
               (116,0xffffffff)]
        for at,value in edits:
            bad=bytearray(raw);struct.pack_into('<I',bad,at,value)
            with self.subTest(at=at,value=value):
                with self.assertRaises(SourceError):parse(bad)
        for length in range(len(raw)):
            with self.subTest(length=length):
                with self.assertRaises(SourceError):parse(raw[:length])
        with self.assertRaises(SourceError):parse(raw+b'\0')

    def test_optional_definition_layouts(self):
        for bit,size in ((8,12),(9,12),(10,8),(11,12),(16,20),(24,8),(25,8),(26,16),(27,4),(28,8),(29,16)):
            raw=resource();raw[88:88]=bytes(size)
            struct.pack_into('<I',raw,16,56+size);struct.pack_into('<I',raw,24,88+size)
            struct.pack_into('<I',raw,32,1<<bit)
            defs,textures=parse(raw);self.assertEqual(defs,(1<<bit,));self.assertEqual(len(textures),1)

    def test_short_random_inputs_are_bounded(self):
        rng=random.Random(0x70617274)
        for _ in range(200):
            raw=rng.randbytes(rng.randrange(1,512))
            with self.assertRaises(SourceError):parse(raw)

    def test_every_reviewed_original_image_against_reference(self):
        if not ROM:self.skipTest('Private supported ROM not supplied')
        import ndspy.rom
        rom=ndspy.rom.NintendoDSRom.fromFile(ROM);count=0;pixels=0
        for identity,raw,profile in reviewed_sources(rom):
            _,textures=parse(raw)
            self.assertEqual(len(textures),profile['images'])
            for index,texture in enumerate(textures):
                with self.subTest(bank=identity,image=index):
                    mask=texture.fmt in (2,3,4)
                    rgba=decode_texture(texture.pixels,texture.palette,texture.width,texture.height,texture.fmt,
                                        zero_transparent=mask,retain_zero_rgb=mask)
                    for zero in ((False,True) if mask else (False,)):
                        expected=oracle_pixels(texture,zero)
                        actual=bytearray(rgba)
                        for at in range(0,len(actual),4):
                            if mask and not zero:actual[at+3]=255
                            # Palette-zero RGB is deliberately retained in the
                            # native bank. Only alpha-discarded RGB is ignored.
                            if mask and zero and not actual[at+3]:actual[at:at+3]=bytes(3)
                        self.assertEqual(bytes(actual),expected)
                    count+=1;pixels+=texture.width*texture.height
        self.assertEqual(count,48)
        print(f'Original particle images: {count}; source texels: {pixels}; all reference comparisons exact')


if __name__=='__main__':
    PARSER=sys.argv.pop(1);ORACLE=sys.argv.pop(1)
    ROM=sys.argv.pop(1) if len(sys.argv)>1 else None
    unittest.main()
