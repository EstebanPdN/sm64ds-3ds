"""Authored, ROM-free boundary checks for the native cooker's source audit."""
import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from native_renderer.audit_models import SourceError, audit_bmd, commands


def packed(operations):
    result = bytearray()
    for at in range(0,len(operations),4):
        batch = operations[at:at+4]
        result.extend(bytes([op for op,args in batch]+[0]*(4-len(batch))))
        for op,args in batch:
            for value in args:
                result.extend(struct.pack('<I',value))
    return result


def fixture():
    stream = packed([(0x40,[0]),(0x14,[0]),(0x23,[0,0]),
                     (0x23,[4096,0]),(0x23,[4096<<16,0]),(0x41,[])])
    data = bytearray(160+len(stream))
    for at,value in ((4,1),(8,64),(12,1),(16,128),(128,1),(132,136),
                     (136,1),(140,152),(144,len(stream)),(148,160)):
        struct.pack_into('<I',data,at,value)
    data[160:] = stream
    return data


class ModelAuditTests(unittest.TestCase):
    def test_source_inventory(self):
        result = audit_bmd(fixture(),'authored-triangle')
        self.assertEqual(result['vertices'],3)
        self.assertEqual(result['commands'],{'40':1,'14':1,'23':3,'41':1})
        self.assertEqual(result['parts'][0]['matrix_slots'],[0])
        self.assertFalse(result['parts'][0]['inherits_initial_matrix'])
        self.assertFalse(result['rendering_ready'])

    def test_range_and_table_bounds(self):
        for at,value in ((16,0xfffffff0),(128,0xffffffff),(148,0xfffffffc),(140,0xffffffff)):
            data = fixture()
            struct.pack_into('<I',data,at,value)
            with self.subTest(offset=at),self.assertRaises(SourceError):
                audit_bmd(data,'corrupt-table')

    def test_truncated_and_unknown_parameters(self):
        for data in (b'\x23\0\0\0',b'\x23\0\0\0'+bytes(4),b'\xfe\0\0\0',bytes(3)):
            with self.subTest(data=data),self.assertRaises(SourceError):
                commands(data)

    def test_inherited_state_is_reported(self):
        result = commands(packed([(0x23,[0,0]),(0x14,[7])]))
        self.assertTrue(result['inherits_initial_matrix'])
        self.assertEqual(result['matrix_slots'],[7])

    def test_palette_admission(self):
        data = fixture()
        # Parameter of the second command (matrix restore) in the first word.
        struct.pack_into('<I',data,168,31)
        with self.assertRaises(SourceError):
            audit_bmd(data,'absent-matrix-slot')


if __name__ == '__main__':
    unittest.main()
