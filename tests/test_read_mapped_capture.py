"""Disk-format bounds and missing-byte rejection; not live-game acceptance."""
import importlib.util,struct,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('read_mapped_capture',ROOT/'tools/read_mapped_capture.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class ReadMappedCapture(unittest.TestCase):
 def make(self,directory,complete=True):
  h=m.HEADER.pack(b'SM64VM1\0',1,64,4,24,1,2,3,99,2 if complete else 0,int(complete),8,1)
  return Path(directory)/'test.vm',h+m.REGION.pack(0,4,1,3,4,0)+b'abcd'+m.REGION.pack(4,4,1,3,4,0)+b'efgh'
 def test_complete_and_cross_region(self):
  with tempfile.TemporaryDirectory() as d:
   p,b=self.make(d);p.write_bytes(b);v=m.MappedCapture(p)
   self.assertTrue(v.complete);self.assertEqual(v.read(2,4),b'cdef');self.assertEqual(v.read(8,0),b'')
   for address,n in [(7,2),(0xffffffff,2),(-1,1),(0,-1)]:
    with self.assertRaises(ValueError):v.read(address,n)
 def test_truncated_payload_never_read_as_full(self):
  with tempfile.TemporaryDirectory() as d:
   p,b=self.make(d,False);p.write_bytes(b[:-2])
   with self.assertRaises(ValueError):m.MappedCapture(p)
   v=m.MappedCapture(p,allow_incomplete=True);self.assertFalse(v.complete)
   self.assertEqual(v.read(2,4),b'cdef');self.assertEqual(v.truncation['captured'],2)
   with self.assertRaises(ValueError):v.read(4,4)
 def test_partial_header_keeps_only_prior_bytes(self):
  with tempfile.TemporaryDirectory() as d:
   p,b=self.make(d,False);p.write_bytes(b[:64+24+4+5]);v=m.MappedCapture(p,allow_incomplete=True)
   self.assertEqual(v.truncation['kind'],'region_header');self.assertEqual(v.read(0,4),b'abcd')
   with self.assertRaises(ValueError):v.read(4,1)
 def test_claimed_complete_is_rejected(self):
  with tempfile.TemporaryDirectory() as d:
   p,b=self.make(d)
   for data in [b[:-1],b+b'X',b[:63],b[:64]+m.REGION.pack(1,4,1,3,4,0)+b[88:]]:
    p.write_bytes(data)
    with self.assertRaises(ValueError):m.MappedCapture(p,allow_incomplete=True)
if __name__=='__main__':unittest.main()
