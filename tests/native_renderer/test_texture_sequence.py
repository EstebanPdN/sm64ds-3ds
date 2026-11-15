"""ROM-free BTP name binding, exact key selection and preparation closure."""
import hashlib
import json
from pathlib import Path
import random
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from native_renderer.texture_sequence import bind,prepare,SourceError,NO_TEXTURE
from native_renderer.cook_model import cook
from native_renderer.animation_profiles import DEFAULT,ROOT
from test_model_audit import packed


def model():
    data=bytearray(224)
    for at,value in ((20,2),(24,56),(28,2),(32,96),(36,2),(40,128)):
        struct.pack_into('<I',data,at,value)
    for table,stride,names in ((56,20,['t0','t1']),(96,16,['p0','p1']),(128,48,['m0','m1'])):
        for i,name in enumerate(names):
            struct.pack_into('<I',data,table+i*stride,len(data));data.extend(name.encode()+b'\0')
    for i in range(2):
        at=56+i*20;struct.pack_into('<4I',data,at+4,len(data),128,8|(8<<16),7<<26)
        data.extend(struct.pack('<H',0x801f if i==0 else 0xfc00)*64)
        struct.pack_into('<II',data,128+i*48+4,0,0)
        struct.pack_into('<I',data,128+i*48+36,(31<<16)|192)
        struct.pack_into('<I',data,128+i*48+40,0xffff)
    # Add one drawable triangle, referencing source material0 and texture0.
    data.extend(bytes((-len(data))%4));bone=len(data);data.extend(bytes(64))
    group=len(data);data.extend(bytes(8));part=len(data);data.extend(bytes(16))
    ids=len(data);data.extend(bytes(4));stream_at=len(data)
    stream=packed([(0x14,[0]),(0x22,[0]),(0x40,[0]),(0x23,[0,0]),
        (0x23,[4096,0]),(0x23,[4096<<16,0]),(0x41,[])])
    data.extend(stream)
    for at,value in ((4,1),(8,bone),(12,1),(16,group),(44,ids),(group,1),(group+4,part),
        (part,1),(part+4,ids),(part+8,len(stream)),(part+12,stream_at),(bone+52,ids),(bone+56,ids)):
        struct.pack_into('<I',data,at,value)
    struct.pack_into('<H',data,bone+48,1)
    return data


def sequence(keys=((0,0,0),(5,1,1)),palette=True,frames=12):
    n=len(keys);data=bytearray(36)
    tex=len(data);data.extend(bytes(16));pal=len(data) if palette else 0
    if palette:data.extend(bytes(16))
    starts=len(data);data.extend(b''.join(struct.pack('<H',k[0]) for k in keys))
    ti=len(data);data.extend(b''.join(struct.pack('<H',k[1]) for k in keys))
    pi=len(data) if palette else 0
    if palette:data.extend(b''.join(struct.pack('<H',k[2]) for k in keys))
    track=len(data);data.extend(struct.pack('<HHIHH',65535,0,0,n,0))
    for address,names in ((tex,['t0','t1']),(pal,['p0','p1'] if palette else []),(track,['m0'])):
        for i,name in enumerate(names):
            struct.pack_into('<I',data,address+i*8+4,len(data));data.extend(name.encode()+b'\0')
    struct.pack_into('<HHIH2xIIIIH2xI',data,0,frames,2,tex,2 if palette else 0,pal,starts,ti,pi,1,track)
    return data


class SequenceTests(unittest.TestCase):
    def test_exact_key_boundaries_and_identity(self):
        source=sequence();bound=bind(model(),source,'authored')
        self.assertEqual(bound.sha256,hashlib.sha256(source).hexdigest())
        for frame in range(12):
            material,key=bound.select(frame)[0]
            self.assertEqual((material,key.texture,key.palette),(0,int(frame>=5),int(frame>=5)))
        for frame in (-1,12):
            with self.assertRaises(SourceError):bound.select(frame)

    def test_unsorted_duplicate_and_unreachable_keys_match_original_reverse_search(self):
        rng=random.Random(4194)
        for _ in range(100):
            keys=[(rng.randrange(16),rng.randrange(2),rng.randrange(2)) for _ in range(20)]
            bound=bind(model(),sequence(keys,frames=12))
            seen=set()
            for frame in range(12):
                eligible=[k for k in keys if k[0]<=frame]
                if eligible:
                    key=bound.select(frame)[0][1];self.assertEqual((key.start,key.texture,key.palette),eligible[-1])
                    seen.add(key)
                else:
                    with self.assertRaises(SourceError):bound.select(frame)
            self.assertEqual(set(bound.tracks[0].reachable(12)),seen)

    def test_palette_free_tracks_include_later_sequence_palette_states(self):
        data=model();first=sequence(((0,1,0),),palette=False)
        second=sequence(((0,0,1),))
        pairs,report=prepare(data,[('inherit',first),('later',second)])
        self.assertEqual(pairs,[(0,1),(1,0),(1,1)])
        self.assertEqual(report[0]['tracks'],[dict(material=0,keys=1,reachable_keys=1)])
        struct.pack_into('<I',data,128+8,NO_TEXTURE)
        pairs,_=prepare(data,[('inherit',first)])
        self.assertEqual(pairs,[(1,NO_TEXTURE)])

    def test_empty_sequence_is_original_noop(self):
        source=bytearray(36);struct.pack_into('<H',source,0,12)
        self.assertEqual(bind(model(),source).select(0),())
        self.assertEqual(prepare(model(),[('empty',source)])[0],[])

    def test_inherited_palette_closure_respects_native_image_budget(self):
        data=model();table=len(data);data.extend(bytes(65*16))
        struct.pack_into('<II',data,28,65,table)
        for i in range(65):
            struct.pack_into('<I',data,table+i*16,len(data));data.extend(f'p{i}'.encode()+b'\0')
        sequences=[('inherit',sequence(((0,1,0),),palette=False))]
        for i in range(65):
            source=sequence(((0,0,1),));pal=struct.unpack_from('<I',source,12)[0]
            struct.pack_into('<I',source,pal+12,len(source));source.extend(f'p{i}'.encode()+b'\0')
            sequences.append((f'explicit-{i}',source))
        with self.assertRaisesRegex(SourceError,'image identity budget'):prepare(data,sequences)

    def test_missing_names_bounds_selectors_and_key_ranges(self):
        source=sequence();header=struct.unpack_from('<HHIH2xIIIIH2xI',source)
        # All source name tables are resolved, including records not selected.
        bad=source.replace(b't1\0',b'xx\0')
        with self.assertRaises(SourceError):bind(model(),bad)
        for at,value in ((4,0),(16,len(source)-1),(20,0),(24,len(source)-1),(32,len(source)),
                         (header[-1]+4,0),(header[2]+4,len(source))):
            bad=bytearray(source);struct.pack_into('<I',bad,at,value)
            with self.subTest(at=at),self.assertRaises(SourceError):bind(model(),bad)
        for at,value in ((0,0),(28,513),(header[6],2),(header[7],2),
                         (header[-1]+8,0),(header[-1]+10,65535)):
            bad=bytearray(source);struct.pack_into('<H',bad,at,value)
            with self.subTest(at=at),self.assertRaises(SourceError):bind(model(),bad)
        for size in range(36):
            with self.assertRaises(SourceError):bind(model(),source[:size])

    def test_cooker_adds_only_new_identities_and_preserves_mesh_uv_basis(self):
        data=model();initial,base,_=cook(data,'authored')
        animated,report,images=cook(data,'authored',[('blink',sequence())])
        self.assertEqual(report['images'],2)
        self.assertEqual([(i['texture'],i['palette']) for i in images],[(0,0),(1,1)])
        self.assertEqual(report['native_material_sources'],base['native_material_sources'])
        first=struct.unpack_from('<I',initial,128)[0];second=struct.unpack_from('<I',animated,128)[0]
        size=struct.unpack_from('<I',initial,132)[0]
        self.assertEqual(initial[first:first+size],animated[second:second+size])
        self.assertEqual(report['prepared_animations'][0]['frames'],12)
        # Different semantic identities must not merge even with equal pixels.
        identical=sequence(((0,0,1),));_,report,_=cook(data,'same-pixels',[('pal',identical)])
        self.assertEqual(report['images'],2)

    def test_reviewed_profile_source_hashes_still_match(self):
        profiles=json.loads(DEFAULT.read_text());self.assertEqual(profiles['version'],1)
        for profile in profiles['models']:
            self.assertTrue(profile['animations'] and profile['evidence'])
            for evidence in profile['evidence']:
                path=ROOT/'third_party/sm64ds-3ds/sm64ds-decomp'/evidence['source']
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),evidence['sha256'])


if __name__=='__main__':unittest.main()
