#!/usr/bin/env python3
"""Mapped snapshot bounds and completeness checks with controlled payloads."""
from pathlib import Path
import sys
import tempfile
import struct
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from inspect_integrated_vm import HEADER,REGION,VM,TRACE_HEADER,TRACE_RECORD,TRACE_RECORD_V2,trace_report,trace_context,LCD,LCD_HEADER,LCD_RECORD,capture_media_report

class SnapshotTests(unittest.TestCase):
    def test_camera_context_preserves_old_flags_and_marks_actual_yaw(self):
        self.assertIsNone(trace_context(0))
        self.assertEqual(trace_context(0x534d0003),dict(pause=True,dialogue=True,cutscene=False))
        c=trace_context(0x534d8c00)
        self.assertEqual(c['camera_sector'],4)
        self.assertEqual(c['camera_yaw_degrees'],180)
        self.assertEqual(c['camera_height_relation'],'above')
    def example(self,complete=1):
        header=HEADER.pack(b'SM64VM1\0',1,64,4,24,1,2,3,4,3,complete,0x40000000,1)
        return header+REGION.pack(0,4096,0,0,0,1)+REGION.pack(4096,4096,3,5,4096,0)+bytes(range(256))*16+REGION.pack(8192,0x40000000-8192,0,0,0,1)
    def parse(self,raw):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'capture.vm';path.write_bytes(raw);return VM(path)
    def test_virtual_address_translation(self):
        vm=self.parse(self.example())
        self.assertEqual(vm.read(4096+253,6),bytes([253,254,255,0,1,2]))
        self.assertEqual(vm.u32(4096),0x03020100)
        with self.assertRaises(AssertionError):vm.read(4095,2)
        with self.assertRaises(AssertionError):vm.read(8190,4)
    def test_trace_versions_and_extra_composition_counters(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'capture.trace'
            for version,record in [(1,TRACE_RECORD),(2,TRACE_RECORD_V2)]:
                fields=[100,10,5,3,0,0]+([0] if version==2 else [])+[7]*(32 if version==1 else 48)
                raw=TRACE_HEADER.pack(b'SM64PR1\0' if version==1 else b'SM64PR2\0',version,record.size,2,0,268111856)+record.pack(*fields)*2
                path.write_bytes(raw);report=trace_report(path)
                self.assertEqual(report['record_count'],2)
                self.assertEqual('CompositeDraw' in report['scenes']['3']['intervals'],version==2)
                self.assertIn('not geometry conversion alone',report['scope_notes']['WorkerMesh'])
            path.write_bytes(raw[:-1])
            with self.assertRaises(AssertionError):trace_report(path)

    def test_version2_captures_only_plain_device_memory(self):
        vram=bytes(range(256))*16
        def raw(version,base):
            header=HEADER.pack(b'SM64VM1\0',version,64,4,24,1,2,3,4,3,1,0x40000000,1)
            return header+REGION.pack(0,base,0,0,0,1)+REGION.pack(base,4096,3,2,4096,4)+vram+REGION.pack(base+4096,0x40000000-base-4096,0,0,0,1)
        vm=self.parse(raw(2,0x1F000000))
        self.assertEqual(vm.read(0x1F000000+5,3),bytes([5,6,7]))
        self.assertEqual(vm.header['version'],2)
        with self.assertRaises(AssertionError):self.parse(raw(1,0x1F000000)) # v1 never carried I/O payload
        with self.assertRaises(AssertionError):self.parse(raw(2,0x1EC00000)) # MMIO registers stay excluded
    def lcd(self):
        header=LCD_HEADER.pack(b'SM64LCD1',1,64,64,3,99,3,7,4,256,0,0,0,0)
        # top-left: BGR8, 2 rows x 3 columns, stride 8; column x stores y bottom-up.
        top=bytearray(24)
        for x in range(3):
            for y in range(2):
                o=x*8+(1-y)*3;top[o:o+3]=bytes([x*10+y,100+y,200+x]) # B,G,R
        bottom=bytearray(8) # RGB565 2x2 stride 4: pure red at (0,0)
        bottom[2:4]=(0xf800).to_bytes(2,'little')
        records=LCD_RECORD.pack(0,0,1,0x18000000,0x1F000000,1,0,8,2,3,24,0,0,0,0,0)
        records+=LCD_RECORD.pack(0,1,0,0,0,0x21,0,0,240,400,0,2,0,0,0,0)
        records+=LCD_RECORD.pack(1,0,1,0x18100000,0x1F100000,2,1,4,2,2,8,0,0,0,0,0)
        registers=struct.pack('<256I',*range(256))
        return header+records+registers+bytes(top)+bytes(bottom)
    def test_physical_lcd_orientation_formats_and_bounds(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'capture.lcd';path.write_bytes(self.lcd());lcd=LCD(path)
            top,right,bottom=lcd.records
            self.assertEqual(lcd.name(right),'top-right');self.assertFalse(right['valid'])
            image=lcd.rgb(top)
            self.assertEqual(len(image),2);self.assertEqual(len(image[0]),3)
            self.assertEqual(image[1][2],(202,101,21)) # R,G,B at x=2,y=1
            self.assertEqual(lcd.rgb(bottom)[0][0],(255,0,0))
            self.assertEqual(lcd.registers['0x400400'],64)
            path.write_bytes(self.lcd()[:-1])
            with self.assertRaises(AssertionError):LCD(path)
            path.write_bytes(self.lcd()+b'x')
            with self.assertRaises(AssertionError):LCD(path)
    def test_lcd_invalid_geometry_never_reaches_image_allocation(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'capture.lcd'
            for field,value in [(0,1),(2,2),(3,0),(4,1),(5,7),(7,4),(8,0),(8,0xffffffff),(9,801),(10,25),(11,4)]:
                with self.subTest(field=field,value=value):
                    raw=bytearray(self.lcd());struct.pack_into('<I',raw,64+field*4,value);path.write_bytes(raw)
                    with self.assertRaises(AssertionError):LCD(path)
            path.write_bytes(self.lcd()[:100])
            with self.assertRaises(AssertionError):LCD(path)
    def test_v2_bundle_requires_complete_vram_and_matching_scanout(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'capture.vm';base=0x1f000000;size=0x600000
            header=HEADER.pack(b'SM64VM1\0',2,64,4,24,1,100,3,7,3,1,0x40000000,1)
            path.write_bytes(header+REGION.pack(0,base,0,0,0,1)+REGION.pack(base,size,3,2,size,4)+bytes(size)+REGION.pack(base+size,0x40000000-base-size,0,0,0,1))
            vm=VM(path);lcd_path=path.with_suffix('.lcd');lcd_path.write_bytes(self.lcd())
            meta={'physical_lcd':True,'console':{'new_model':False,'fps_lock':1}}
            report=capture_media_report(vm,path,meta)
            self.assertEqual(report['vram_bytes'],size)
            self.assertEqual(report['screens'],['top-left','bottom'])
            self.assertFalse(report['physical_console_validation'])
            with self.assertRaises(AssertionError):capture_media_report(vm,path,{'physical_lcd':False})
            for offset,value in [(24,101),(32,4),(36,8)]:
                bad=bytearray(self.lcd());struct.pack_into('<I',bad,offset,value);lcd_path.write_bytes(bad)
                with self.assertRaises(AssertionError):capture_media_report(vm,path,meta)
            lcd_path.write_bytes(self.lcd())
            path.write_bytes(header+REGION.pack(0,base,0,0,0,1)+REGION.pack(base,4096,3,2,4096,4)+bytes(4096)+REGION.pack(base+4096,0x40000000-base-4096,0,0,0,1))
            with self.assertRaises(AssertionError):capture_media_report(VM(path),path,meta)
            self.assertFalse(capture_media_report(self.parse(self.example()),path,{})['available'])
    def test_incomplete_or_truncated_never_accepted(self):
        with self.assertRaises(AssertionError):self.parse(self.example(0))
        with self.assertRaises((AssertionError,struct.error)):
            self.parse(self.example()[:-1])
        with self.assertRaises(AssertionError):self.parse(self.example()+b'extra')

if __name__=='__main__':unittest.main()
