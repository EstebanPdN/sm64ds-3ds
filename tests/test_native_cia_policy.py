"""The native-test title must use the maintained Old/New launch policy."""
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from verify_integrated_cia import NATIVE_TEST_TITLE_ID,REQUIRED_SERVICES,REQUIRED_SVCS,profile


class NativeCiaPolicyTests(unittest.TestCase):
    def header(self):
        ex=bytearray(0x400)
        struct.pack_into('<Q',ex,0x200,NATIVE_TEST_TITLE_ID)
        ex[0x0d]=2
        struct.pack_into('<I',ex,0x1c,0x100000)
        ex[0x20c:0x20f]=bytes([3,1,0x3c])
        struct.pack_into('<H',ex,0x210,0xD0) # MaxCpu: core 1 up to 80%
        ex[0x248:0x24f]=(0x8084).to_bytes(7,'little')
        struct.pack_into('<I',ex,0x3f0,0x300)
        for i,service in enumerate(sorted(REQUIRED_SERVICES)):
            ex[0x250+i*8:0x258+i*8]=service.encode().ljust(8,b'\0')
        descriptors=[0xff002100,0xff81ff00,0xff81ff80]
        for group in range(5):
            bits=sum(1<<(svc%24) for svc in REQUIRED_SVCS if svc//24==group)
            if bits:descriptors.append(0xf0000000|(group<<24)|bits)
        struct.pack_into('<28I',ex,0x370,*descriptors,*([0xffffffff]*(28-len(descriptors))))
        return ex

    def test_native_test_has_same_safe_old_and_new_policy(self):
        checked=profile(self.header(),NATIVE_TEST_TITLE_ID)
        self.assertEqual(checked['system_mode'],'80MB')
        self.assertEqual(checked['system_mode_ext'],'124MB')
        self.assertTrue(checked['can_access_core2'])

    def test_rejects_96mb_regression_before_hardware(self):
        ex=self.header();ex[0x20e]=0x2c
        with self.assertRaisesRegex(ValueError,'Old 80MB'):
            profile(ex,NATIVE_TEST_TITLE_ID)

    def test_old_3ds_render_worker_may_ask_for_80_percent_of_core_1(self):
        checked=profile(self.header(),NATIVE_TEST_TITLE_ID)
        self.assertEqual(checked['core1_cpu_percent_max'],80)
        ex=self.header();struct.pack_into('<H',ex,0x210,0x9E) # BUILD123: 30%
        with self.assertRaisesRegex(ValueError,'MaxCpu'):
            profile(ex,NATIVE_TEST_TITLE_ID)

    def test_rejects_other_launch_contract_regressions(self):
        for offset,value in [(0x20c,0),(0x20d,0),(0x248,0xff),(0x371,1)]:
            with self.subTest(offset=offset):
                ex=self.header();ex[offset]=value
                with self.assertRaises(ValueError):profile(ex,NATIVE_TEST_TITLE_ID)


if __name__=='__main__':unittest.main()
