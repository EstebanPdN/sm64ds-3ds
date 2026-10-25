#!/usr/bin/env python3
"""Source-contract and executable parent/selection tests; no emulator needed."""
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('options_hooks', ROOT/'tools/prepare_integrated_options_hooks.py')
hooks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hooks)
VENDOR = ROOT/'third_party/sm64ds-3ds/sm64ds-decomp'


class OptionsHooks(unittest.TestCase):
    def test_copy_patch_duplicate_and_state(self):
        original = (VENDOR/hooks.SOURCE).read_text()
        before = hashlib.sha256(original.encode()).hexdigest()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); path = root/hooks.SOURCE
            path.parent.mkdir(); path.write_text(original)
            title=root/'src/func_ov007_020b63e4.c'; title.write_text((VENDOR/'src/func_ov007_020b63e4.c').read_text())
            self.assertEqual(hooks.patch_options_sources(root), [hooks.SOURCE, 'src/func_ov007_020b63e4.c'])
            patched = path.read_text()
            self.assertEqual(patched.count('sm64ds_options_request('), 3)
            with self.assertRaisesRegex(ValueError, 'already patched'):
                hooks.patch_options_sources(root)
            self.assertEqual(path.read_text(), patched)
            # Execute the exact injected statements with each parent and every
            # original selection; no fake DS memory-mapped registers needed.
            cases = []
            for entry in (7, 9):
                chunk = patched.split(f'    case {entry}: {{\n', 1)[1]
                chunk = chunk.split('#endif', 1)[0].split('*/', 1)[1]
                cases.append(f'void entry{entry}(){{{chunk}}}')
            program = '''#include <cassert>
int data_0209f1ec,data_0209f248,data_0209f218,data_0209f2e0;
int request=-1;
extern "C" void sm64ds_options_request(int p){request=p;}
''' + '\n'.join(cases) + '''
int main(){for(int vs=0;vs!=2;++vs)for(int sel=0;sel!=4;++sel){
data_0209f218=vs;data_0209f2e0=sel;data_0209f1ec=data_0209f248=7;entry7();
assert(request==1);assert(data_0209f1ec==(vs?5:1));assert(data_0209f248==data_0209f1ec);assert(data_0209f2e0==sel);
data_0209f1ec=data_0209f248=9;entry9();assert(request==0);assert(data_0209f1ec==(vs?5:1));assert(data_0209f248==data_0209f1ec);assert(data_0209f2e0==sel);
}}
'''
            cpp=root/'fixture.cpp';cpp.write_text(program)
            subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(cpp),'-o',str(root/'fixture')],check=True)
            subprocess.run([str(root/'fixture')],check=True)
        self.assertEqual(hashlib.sha256((VENDOR/hooks.SOURCE).read_bytes()).hexdigest(), before)

    def test_vendor_rejected(self):
        with self.assertRaisesRegex(ValueError, 'vendor'):
            hooks.patch_options_sources(VENDOR)

    def test_missing_anchor_does_not_write(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);path=root/hooks.SOURCE;path.parent.mkdir()
            original=(VENDOR/hooks.SOURCE).read_text().replace('    case 9: {','    case 99: {')
            path.write_text(original)
            with self.assertRaisesRegex(ValueError,'contract changed'):
                hooks.patch_options_sources(root)
            self.assertEqual(path.read_text(),original)

    def test_symlink_to_vendor_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);path=root/hooks.SOURCE;path.parent.mkdir()
            path.symlink_to(VENDOR/hooks.SOURCE)
            with self.assertRaisesRegex(ValueError,'vendor'):
                hooks.patch_options_sources(root)


if __name__=='__main__':
    unittest.main()
