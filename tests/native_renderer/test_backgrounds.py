"""Authored sparse tile/atlas contracts and original archive-member resolution."""
from pathlib import Path
import hashlib
import importlib.util
import struct
import sys
import types
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from native_renderer.nitro2d import TileMap,tilemap,SourceError
from native_renderer.backgrounds import tile_rgba,prepare,prepare_variants,palette_variants
from native_renderer.source_members import menu_member
from native_renderer.menu_backdrops import sub_palettes,repeating_gradient


def container(records):
    body=b''.join(k+struct.pack('<I',8+len(v))+v for k,v in records)
    return struct.pack('<4sHHIHH',b'NCSC',0xfeff,0x101,16+len(body),16,len(records))+body


# Archive-member resolution decodes NARC/LZ10 through ndspy, a pinned build
# dependency (docs/BUILDING.md). Without it those two cases are reported as
# skipped; the authored tile, palette and atlas contracts still run.
HAS_NDSPY=importlib.util.find_spec('ndspy') is not None


class BackgroundTests(unittest.TestCase):
    def setUp(self):
        self.characters=bytes([0x21]*32+[0x00]*32)
        self.palette=bytearray(512)
        struct.pack_into('<H',self.palette,2,31)
        struct.pack_into('<H',self.palette,4,31<<5)
        struct.pack_into('<H',self.palette,(16+1)*2,31<<10)

    def test_counted_screen_reader(self):
        raw=container([(b'SCRN',struct.pack('<4I2H',2,1,0,1,0,0x0401))])
        screen=tilemap(raw)
        self.assertEqual((screen.width,screen.height,screen.fields,screen.entries),(2,1,(0,1),(0,0x0401)))
        for bad in [raw[:-1],container([(b'SCRN',struct.pack('<4I',2,1,0,1))]),
                    container([(b'SCRN',struct.pack('<4I',129,1,0,1))]),
                    container([(b'SCRN',struct.pack('<4I2H',2,1,1,1,0,0))])]:
            with self.assertRaises(SourceError):tilemap(bad)

    def test_source_palette_flip_and_binary_mask(self):
        normal=tile_rgba(self.characters,self.palette,0)
        self.assertEqual(normal[:8],bytes([251,0,0,255,0,251,0,255]))
        flipped=tile_rgba(self.characters,self.palette,1024)
        self.assertEqual(flipped[:8],bytes([0,251,0,255,251,0,0,255]))
        self.assertEqual(tile_rgba(self.characters,self.palette,1),bytes(256))
        self.assertEqual(tile_rgba(self.characters,self.palette,0x1000)[:4],bytes([0,0,251,255]))
        with self.assertRaises(SourceError):tile_rgba(self.characters,self.palette,2)
        self.assertEqual(tile_rgba(bytes(range(64)),self.palette,0xf000,True)[4:8],bytes([251,0,0,255]))

    def test_sparse_geometry_deduplication_and_exact_atlas_reconstruction(self):
        screen=TileMap(4,1,(0,0),(0,0,1,0x400))
        native,report,preview,atlas=prepare(screen,self.characters,self.palette,bytes(range(32)))
        self.assertEqual((report['visible_tiles'],report['unique_tiles'],report['atlas_width'],report['atlas_height']),(3,2,16,8))
        self.assertEqual(report['geometry_bytes'],3*108)
        self.assertEqual(report['native_gpu_payload'],512+512) # two128-aligned GPU allocations
        # Independently reconstruct all source pixels, including transparent
        # holes and the duplicated/flipped instances, then compare the preview.
        expected=bytearray(32*8*4)
        for y in range(8):
            for x in range(32):
                if x//8==2:continue
                red=(x%2==0) if x<24 else (x%2==1)
                at=(y*32+x)*4;expected[at:at+4]=bytes([251,0,0,255] if red else [0,251,0,255])
        self.assertEqual(preview,bytes(expected))
        mt=struct.unpack_from('<I',native,32)[0];vertex_at,vertex_bytes,index_at,index_bytes=struct.unpack_from('<4I',native,mt)
        self.assertEqual((vertex_bytes,index_bytes),(3*4*24,3*6*2))
        for q,source_x in enumerate([0,1,3]):
            vertices=list(struct.iter_unpack('<5f4B',native[vertex_at+q*96:vertex_at+(q+1)*96]))
            self.assertEqual([v[0] for v in vertices],[source_x/4,(source_x+1)/4,(source_x+1)/4,source_x/4])
            for py in range(8):
                for px in range(8):
                    u=vertices[0][3]+(vertices[1][3]-vertices[0][3])*(px+.5)/8
                    v=vertices[0][4]+(vertices[3][4]-vertices[0][4])*(py+.5)/8
                    at=(int(v*8)*16+int(u*16))*4
                    source=(py*32+source_x*8+px)*4
                    self.assertEqual(atlas[at:at+4],preview[source:source+4])
        self.assertEqual(prepare(screen,self.characters,self.palette,bytes(range(32)))[0],native)
        with self.assertRaises(SourceError):prepare(TileMap(1,1,(0,0),(1,)),self.characters,self.palette,bytes(32))

    def test_shared_variant_atlas_and_geometry(self):
        a=TileMap(4,1,(0,0),(0,0,1,0x400));b=TileMap(4,1,(0,0),(0x1000,0x1000,1,0x1400))
        native,report,previews,atlas=prepare_variants((a,b,b),self.characters,self.palette,bytes(32))
        self.assertEqual((report['variants'],report['parts'],report['meshes']),(3,3,2))
        self.assertEqual(previews[0],prepare(a,self.characters,self.palette,bytes(32))[2])
        self.assertEqual(previews[1],prepare(b,self.characters,self.palette,bytes(32))[2])
        self.assertEqual(previews[1],previews[2]);self.assertEqual(report['bounds'],[[0,0,32,8]]*3)
        self.assertEqual(prepare_variants((a,b,b),self.characters,self.palette,bytes(32))[0],native)
        self.assertTrue(atlas)
        with self.assertRaises(SourceError):prepare_variants((a,TileMap(1,1,(0,0),(0,))),self.characters,self.palette,bytes(32))
        with self.assertRaises(SourceError):prepare_variants((),self.characters,self.palette,bytes(32))

    def test_eight_bit_palette_variants_keep_indices_flips_and_geometry(self):
        screen=TileMap(2,1,(0,1),(0,0xfc00))
        pixels=bytes(range(64))
        a=b''.join(struct.pack('<H',i&31) for i in range(256))
        b=b''.join(struct.pack('<H',(i&31)<<5) for i in range(256))
        native,report,previews,_=prepare_variants((screen,screen),pixels,a,bytes(32),True,(a,b))
        self.assertEqual(previews[0],prepare(screen,pixels,a,bytes(32),True)[2])
        self.assertEqual(previews[1],prepare(screen,pixels,b,bytes(32),True)[2])
        self.assertNotEqual(previews[0],previews[1]);self.assertEqual(report['parts'],2)
        self.assertTrue(native)
        with self.assertRaises(SourceError):prepare_variants((screen,screen),pixels,a,bytes(32),True,(a,))

    def test_partial_sub_palette_load_preserves_suffix_and_font_override(self):
        base=bytes([3])*512;override=bytes(range(10))
        variants=sub_palettes(base,[bytes([7])*512,bytes([9])*512],override)
        self.assertEqual(variants[0],bytes([3])*502+override)
        self.assertEqual(variants[1],bytes([7])*416+bytes([3])*86+override)
        self.assertEqual(variants[2],bytes([9])*416+bytes([3])*86+override)

    def test_wide_repeat_admits_dithered_gradients_and_excludes_art(self):
        preview=bytearray(bytes([40,120,248,255])*256*192+bytes(256*64*4))
        preview[4:8]=bytes([48,120,248,255])
        self.assertTrue(repeating_gradient(bytes(preview)))
        preview[4:8]=bytes([57,120,248,255]);self.assertTrue(repeating_gradient(bytes(preview)))
        preview[4:8]=bytes([200,120,248,255]);self.assertFalse(repeating_gradient(bytes(preview)))
        preview[4:8]=bytes([40,120,248,0]);self.assertFalse(repeating_gradient(bytes(preview)))

    def test_reviewed_palette_writes_preserve_low_bits(self):
        source=TileMap(32,32,(0,0),tuple((i*7)&65535 for i in range(1024)))
        states=palette_variants(source);self.assertEqual(len(states),5)
        for mode,screen in enumerate(states[1:]):
            for at,(before,after) in enumerate(zip(source.entries,screen.entries)):
                row=next((n for n,offset in enumerate([0x106,0x146,0x286,0x2c6,0x386,0x3c6]) if offset//2<=at<offset//2+32),None)
                self.assertEqual(after&0xfff,before&0xfff)
                if row is None:self.assertEqual(after,before)
                else:self.assertEqual(after>>12,4 if row//2==mode else 3)


class MembersTests(unittest.TestCase):
    def rom(self,handle,data):
        menu=bytearray(92);struct.pack_into('<H',menu,0x26*2,handle)
        names=struct.pack('<I',0x020bd4bc)+b'asset.bin\0'
        lookup={'asset.bin':0}
        return types.SimpleNamespace(files=[data],filenames=types.SimpleNamespace(idOf=lookup.get),
            arm9RamAddress=0x02004000,arm9=b'',loadArm9Overlays=lambda ids:{0:types.SimpleNamespace(ramAddress=0x020bd4b8,data=names),7:types.SimpleNamespace(ramAddress=0x020d7584,data=menu)})

    @unittest.skipUnless(HAS_NDSPY,'ndspy is not installed')
    def test_explicit_direct_member_and_compression_contract(self):
        rom=self.rom(0,b'abc');member=menu_member(rom,0x26)
        self.assertEqual((member.handle,member.path,member.data),(0,'asset.bin',b'abc'))
        self.assertEqual(member.source_sha256,hashlib.sha256(b'abc').hexdigest())
        for index in [0,46,-1,True]:
            with self.assertRaises(SourceError):menu_member(rom,index)
        with self.assertRaises(SourceError):menu_member(rom,0x26,decompress=True)
        rom.files[0]=b'\x10\x04\0\0\x00abcd'
        self.assertEqual(menu_member(rom,0x26,decompress=True).data,b'abcd')
        rom.files[0]=b'\x10\x04\0\0'
        with self.assertRaises(SourceError):menu_member(rom,0x26,decompress=True)

    @unittest.skipUnless(HAS_NDSPY,'ndspy is not installed')
    def test_original_archive_ranges_and_member_names(self):
        import ndspy.narc
        import ndspy.fnt
        arc=ndspy.narc.NARC.fromFilesAndNames([b'original'],ndspy.fnt.Folder(files=['font.bin']))
        rom=self.rom(0x8000,arc.save());table=0x0208ecf4-rom.arm9RamAddress;image=bytearray(table+13*20+1024)
        at=table+13*20
        for slot in range(13):
            text=f'/ARCHIVE/a{slot}.narc'.encode()+b'\0';p=at+rom.arm9RamAddress;image[at:at+len(text)]=text;at+=len(text)
            first=0x8000+slot*0x400;struct.pack_into('<IIHHII',image,table+slot*20,0,0,first,first+(slot==0),0,p)
        rom.arm9=bytes(image);rom.filenames=types.SimpleNamespace(idOf={'ARCHIVE/a0.narc':0}.get)
        member=menu_member(rom,0x26)
        self.assertEqual((member.path,member.data),('ARCHIVE/a0.narc:font.bin',b'original'))
        bad=bytearray(image);struct.pack_into('<H',bad,table+8,0x8001);rom.arm9=bytes(bad)
        with self.assertRaises(SourceError):menu_member(rom,0x26)
        rom.arm9=bytes(image);rom.files[0]=ndspy.narc.NARC.fromFilesAndNames([b'a',b'b'],ndspy.fnt.Folder(files=['a','b'])).save()
        with self.assertRaises(SourceError):menu_member(rom,0x26)


if __name__=='__main__':unittest.main()
