"""Differential gate for the generated native map publication, without Azahar."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from prepare_integrated_native_renderer import native_affine_ppu_patch,affine_memo_patch,ppu_cost_cuts_patch,gpu_alpha_patch,gpu_layers_patch


class AffineFrontTests(unittest.TestCase):
    def test_owned_foreground_matches_full_cpu_ppu(self):
        port=ROOT/'third_party/sm64ds-3ds/port'
        source,header=native_affine_ppu_patch((port/'ntr/ppu.cpp').read_text(),(port/'ntr/include/ntr/ppu.h').read_text())
        # BUILD142: the memo fast paths (affine and scroll-only), as the
        # preparer orders them.
        source=ppu_cost_cuts_patch(affine_memo_patch(source))
        source,header=gpu_alpha_patch(source,header)
        source,header=gpu_layers_patch(source,header)
        with tempfile.TemporaryDirectory(prefix='native-affine-ppu-',dir=ROOT/'build/native-renderer') as temporary:
            folder=Path(temporary);(folder/'ntr').mkdir();(folder/'ntr/ppu.h').write_text(header)
            (folder/'ppu.cpp').write_text(source)
            command=[shutil.which('clang++') or shutil.which('c++'),'-std=c++17','-O2','-g','-D__3DS__',
                '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
                '-I'+str(folder),'-I'+str(port/'ntr/include'),
                str(ROOT/'tests/native_renderer/affine_front_reference.cpp'),str(folder/'ppu.cpp'),
                str(port/'ntr/vram_banks.cpp'),str(port/'ntr/bitmap.cpp'),'-o',str(folder/'reference')]
            built=subprocess.run(command,text=True,capture_output=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            result=subprocess.run([str(folder/'reference')],text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('7962624 pixels checked',result.stdout)
            self.assertIn('scroll fast path hits=8',result.stdout)


if __name__=='__main__':unittest.main()
