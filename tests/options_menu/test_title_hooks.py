#!/usr/bin/env python3
"""Title selection contract validation; never modifies vendor."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
PROPOSAL=ROOT/'tools/prepare_integrated_options_hooks.py'
spec=importlib.util.spec_from_file_location('title_proposal',PROPOSAL)
hooks=importlib.util.module_from_spec(spec);spec.loader.exec_module(hooks)
VENDOR=ROOT/'third_party/sm64ds-3ds/sm64ds-decomp'
TITLE='src/func_ov007_020b63e4.c'

class TitleHook(unittest.TestCase):
 def clone(self,root):
  for name in (hooks.SOURCE,TITLE):
   p=root/name;p.parent.mkdir(exist_ok=True);p.write_text((VENDOR/name).read_text())
 def test_patch_paths_guard_and_exact_native_callback(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);self.clone(root)
   self.assertEqual(hooks.patch_options_sources(root),[hooks.SOURCE,TITLE])
   text=(root/TITLE).read_text()
   guard=text.split('    unsigned short v = *(unsigned short *)a[0];\n',1)[1].split('#endif',1)[0]
   guard=guard.split('*/',1)[1]
   program='''#include <cassert>
int request=-1,original=0;
extern "C" void sm64ds_options_request(int p){request=p;}
void selected(unsigned short v){'''+guard+'''++original;}
int main(){for(unsigned short id=0;id!=24;++id){request=-1;original=0;selected(id);
assert(request==(id==7?0:-1));assert(original==(id==7?0:1));}}
'''
   cpp=root/'selected.cpp';cpp.write_text(program)
   subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(cpp),'-o',str(root/'selected')],check=True)
   subprocess.run([str(root/'selected')],check=True)
   with self.assertRaisesRegex(ValueError,'already patched'):hooks.patch_options_sources(root)
 def test_all_validation_precedes_any_write(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);self.clone(root)
   stage=(root/hooks.SOURCE).read_text();title=(root/TITLE).read_text().replace('    unsigned short v = *(unsigned short *)a[0];','    unsigned short v = 99;')
   (root/TITLE).write_text(title)
   with self.assertRaisesRegex(ValueError,'contract changed'):hooks.patch_options_sources(root)
   self.assertEqual((root/hooks.SOURCE).read_text(),stage)
   self.assertEqual((root/TITLE).read_text(),title)
 def test_vendor_and_title_symlink_refused(self):
  with self.assertRaisesRegex(ValueError,'vendor'):hooks.patch_options_sources(VENDOR)
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);self.clone(root);(root/TITLE).unlink();(root/TITLE).symlink_to(VENDOR/TITLE)
   stage=(root/hooks.SOURCE).read_text()
   with self.assertRaisesRegex(ValueError,'vendor'):hooks.patch_options_sources(root)
   self.assertEqual((root/hooks.SOURCE).read_text(),stage)
 def test_original_contract_connects_element7_to_options8(self):
  animation=(VENDOR/'src/func_ov007_020b50e8.c').read_text()
  self.assertIn('case 7:\n            pv[1] = 8;',animation)
  dispatch=(VENDOR/'src/func_ov007_020b0548.c').read_text()
  self.assertIn('case 8: func_ov007_020b11bc(); func_ov007_020b1224();',dispatch)
  options=(VENDOR/'src/func_ov007_020b1224.c').read_text()
  self.assertIn('case 9: case 10: case 11:',options)
  self.assertIn('case 12: case 13:',options)
  self.assertIn('((void (*)(int))*(int *)(data_ov007_0210342c + 0x2c))(4);',animation)
  self.assertIn('((void (*)(int))*(int *)(data_ov007_0210342c + 0x2c))(5);',animation)

if __name__=='__main__':unittest.main()
