"""ROM-free geometry semantics and refusal boundaries for host preparation."""
import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from native_renderer.cook_model import geometry, cook, NeedsNativeFeature, SourceError, NormalUvState
from test_model_audit import packed


def vertex(x,y=0,z=0):
    return (0x23,[(x&65535)|((y&65535)<<16),z&65535])


def convert(operations,bones=(0,),mode=0,diffuse=0xffff,emission=0,lights=0):
    return geometry(packed([(0x14,[0]),(0x22,[0]),(0x40,[mode]),*operations,(0x41,[])]),
                    bones,32,16,1,diffuse,emission,lights)


def decoded(mesh):
    vertices=list(struct.iter_unpack('<5f4B',mesh.vertices))
    indices=[i[0] for i in struct.iter_unpack('<H',mesh.indices)]
    return vertices,indices


class GeometryCookerTests(unittest.TestCase):
    def test_primitive_topology(self):
        for mode,count,expected in ((0,6,[0,1,2,3,4,5]),(1,4,[0,1,2,0,2,3]),
            (2,5,[0,1,2,2,1,3,2,3,4]),(3,6,[0,1,2,0,2,3,3,2,4,3,4,5])):
            mesh,runs=convert([vertex(i*100) for i in range(count)],mode=mode)
            values,indices=decoded(mesh)
            self.assertEqual(indices,expected)
            # Quad strips visit source vertex 3 before 2; dedup follows draw order.
            order=[0,1,3,2,5,4] if mode==3 else list(range(count))
            self.assertEqual([round(v[0]*4096) for v in values],[i*100 for i in order])
            self.assertEqual(runs,[[0,'diffuse',0,len(indices)]])

    def test_vertex_encodings_and_signed_wrap(self):
        operations=[vertex(32767,-100,200),(0x28,[1|(1023<<10)|(2<<20)]),
            (0x25,[(25&65535)|((30&65535)<<16)]),(0x26,[40|(50<<16)]),
            (0x27,[60|(70<<16)]),(0x24,[(1023)|(2<<10)|(1021<<20)])]
        mesh,_=convert(operations)
        values,_=decoded(mesh)
        positions=[tuple(round(c*4096) for c in v[:3]) for v in values]
        self.assertEqual(positions,[(32767,-100,200),(-32768,-101,202),(25,30,202),
                                    (40,30,50),(40,60,70),(-64,128,-192)])

    def test_uv_fixed_units_and_resource_scale(self):
        ops=[(0x14,[0]),(0x22,[(512&65535)|((-256&65535)<<16)]),(0x40,[0]),
             vertex(4096),vertex(0,4096),vertex(0,0,4096),(0x41,[])]
        mesh,_=geometry(packed(ops),(0,),32,16,8,0xffff,0,0)
        values,_=decoded(mesh)
        self.assertEqual(values[0][:5],(8,0,0,1,-1))

    def test_live_colours_are_not_baked(self):
        triangle=[vertex(0),vertex(100),vertex(200)]
        mesh,runs=convert([*triangle,(0x21,[0]),*triangle,(0x20,[31]),*triangle,
                           (0x30,[0x8000|992]),*triangle],diffuse=0x801f,emission=31<<26)
        values,indices=decoded(mesh)
        self.assertEqual(runs,[[0,'diffuse',0,3],[0,'emission',3,3],[0,'literal',6,6]])
        self.assertTrue(all(v[5:]==(255,255,255,255) for v in values[:6]))
        self.assertTrue(all(v[5:]==(255,0,0,255) for v in values[6:9]))
        self.assertTrue(all(v[5:]==(0,255,0,255) for v in values[9:12]))
        self.assertEqual(len(indices),12)

    def test_bone_runs_preserve_source_order(self):
        tri=[vertex(0),vertex(100),vertex(200)]
        mesh,runs=convert([*tri,(0x14,[1]),*tri,(0x14,[0]),*tri],bones=(4,2))
        self.assertEqual(runs,[[4,'diffuse',0,3],[2,'diffuse',3,3],[4,'diffuse',6,3]])
        self.assertEqual(len(mesh.vertices)//24,6)

    def test_unsupported_state_is_explicit(self):
        cases=[([vertex(0),(0x14,[1]),vertex(100),vertex(200)],dict(bones=(0,1))),
            ([vertex(0),(0x20,[31]),vertex(100),vertex(200)],{}),
            ([(0x28,[0]),vertex(100),vertex(200)],{}),
            ([vertex(0),vertex(100),vertex(200)],dict(diffuse=0))]
        for ops,args in cases:
            with self.subTest(args=args),self.assertRaises(NeedsNativeFeature):convert(ops,**args)

    def test_lighting_latches_normal_basis_and_inline_coefficients(self):
        tri=[vertex(0),vertex(100),vertex(200)]
        mesh,runs=convert([(0x21,[511]),(0x14,[1]),*tri,(0x30,[0x12345678]),*tri,
            (0x21,[512]),*tri,(0x20,[31]),*tri],bones=(4,2),lights=1)
        self.assertTrue(mesh.normals)
        self.assertEqual(runs,[[2,('lighting',None,4),0,6],[2,('lighting',0x12345678,2),6,3],[2,'literal',9,3]])
        records=list(struct.iter_unpack('<5f4B4h',mesh.vertices))
        self.assertEqual(records[0][9:12],(511,0,0))
        self.assertEqual(records[3][9:12],(-512,0,0))
        self.assertEqual(records[-1][5:9],(255,0,0,255))
        # Light flags alone never enlarge geometry that only uses COLOR.
        mesh,_=convert([(0x20,[31]),*tri],lights=1)
        self.assertFalse(mesh.normals)

    def test_lighting_mixed_normal_bases_need_a_palette(self):
        with self.assertRaisesRegex(NeedsNativeFeature,'normal basis palette'):
            convert([(0x21,[0]),vertex(0),(0x14,[1]),(0x21,[0]),(0x14,[0]),vertex(100),vertex(200)],bones=(0,1),lights=1)

    def test_normal_uv_normal_and_texcoord_update_retained_coordinates(self):
        ops=[(0x14,[0]),(0x40,[0]),(0x21,[511|(512<<10)|(1023<<20)]),vertex(0),
             (0x22,[32|(65520<<16)]),vertex(100),(0x21,[0]),vertex(200),(0x41,[])]
        mesh,_=geometry(packed(ops),(0,),32,16,1,0xffff,0,0,True,True)
        self.assertTrue(mesh.normals)
        records=list(struct.iter_unpack('<5f4B4h',mesh.vertices))
        self.assertEqual(records[0][9:],(511,-512,-1,1))
        self.assertEqual(records[1][3:5],(32/(16*32),-16/(16*16)))
        self.assertEqual(records[1][9:],(511,-512,-1,2))
        self.assertEqual(records[2][3:5],(32/(16*32),-16/(16*16)))
        self.assertEqual(records[2][9:],(0,0,0,0))
        with self.assertRaises(NeedsNativeFeature):
            geometry(packed([(0x14,[0]),(0x40,[0]),vertex(0),vertex(1),vertex(2),(0x41,[])]),(0,),8,8,1,0xffff,0,0,True,True)

    def test_normal_uv_explicit_offset_carries_between_group_parts(self):
        state=NormalUvState()
        first=packed([(0x14,[0]),(0x21,[0]),(0x40,[0]),vertex(0),vertex(1),vertex(2),(0x41,[]),(0x22,[123|(456<<16)])])
        second=packed([(0x14,[0]),(0x21,[0]),(0x40,[0]),vertex(0),vertex(1),vertex(2),(0x41,[])])
        a,_=geometry(first,(0,),8,8,1,0xffff,0,0,True,True,state)
        self.assertEqual(state.raw_uv,(123,456))
        b,_=geometry(second,(0,),8,8,1,0xffff,0,0,True,True,state)
        va=next(struct.iter_unpack('<5f4B4h',a.vertices));vb=next(struct.iter_unpack('<5f4B4h',b.vertices))
        self.assertEqual(va[3:5],(0,0));self.assertEqual(va[12],1)
        self.assertEqual(vb[3:5],(123/128,456/128));self.assertEqual(vb[12],0)
        # A new material/group reinstalls its live offset.
        c,_=geometry(second,(0,),8,8,1,0xffff,0,0,True,True,NormalUvState())
        self.assertEqual(next(struct.iter_unpack('<5f4B4h',c.vertices))[12],1)

    def test_incomplete_and_truncated_geometry(self):
        for ops in ([vertex(0)],[vertex(0),vertex(100)],[(0x40,[1])]):
            with self.assertRaises(SourceError):convert(ops)
        with self.assertRaises(SourceError):geometry(b'\x23\0\0\0',(0,),8,8,1,0xffff,0,0)

    def test_untextured_resource_and_live_material_provenance(self):
        # One source node/group/material and a triangle, with no texture or UV
        # commands. All pointers are authored file offsets, not process data.
        stream=packed([(0x14,[0]),(0x40,[0]),vertex(0),vertex(4096),vertex(0,4096),(0x41,[])])
        data=bytearray(224)+stream
        for at,value in ((4,1),(8,64),(12,1),(16,128),(36,1),(40,152),(44,202),
                         (116,200),(120,201),(128,1),(132,136),(136,1),(140,204),
                         (144,len(stream)),(148,224),(156,0xffffffff),(188,31<<16|192),(192,0x801f)):
            struct.pack_into('<I',data,at,value)
        struct.pack_into('<H',data,112,1)
        data[200]=data[201]=data[202]=data[204]=0
        native,report,images=cook(data,'authored-untextured')
        self.assertEqual(report['images'],0)
        self.assertEqual(images,[])
        self.assertEqual(report['native_material_sources'],[dict(source_material=0,colour_source='diffuse',width=1,height=1,uv_transform=False,normal_uv=False)])
        self.assertEqual(report['native_part_sources'],[dict(bone=0,material=0,billboard=False,normal_bone=65535)])
        self.assertTrue(report['native_material_bindings_required'])
        material_at=struct.unpack_from('<I',native,40)[0]
        self.assertEqual(struct.unpack_from('<I',native,material_at)[0],0xffffffff)
        self.assertEqual(struct.unpack_from('<I',native,material_at+44)[0],0xff0000ff)
        vb=struct.unpack_from('<I',native,128)[0]
        self.assertEqual(native[vb+20:vb+24],bytes([255]*4))


if __name__=='__main__':unittest.main()
