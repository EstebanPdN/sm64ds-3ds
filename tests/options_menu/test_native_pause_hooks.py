#!/usr/bin/env python3
import importlib.util,tempfile,unittest,subprocess,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];VENDOR=ROOT/'third_party/sm64ds-3ds/sm64ds-decomp'
s=importlib.util.spec_from_file_location('native_pause',ROOT/'tools/prepare_integrated_native_pause.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class NativePauseHooks(unittest.TestCase):
 def clone(self,root):
  for f in m.FILES:
   p=root/f;p.parent.mkdir(exist_ok=True);p.write_text((VENDOR/f).read_text())
 def test_copy_and_exact_states(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);self.clone(root);self.assertEqual(m.patch_sources(root),m.FILES)
   wrench=(root/m.FILES[5]).read_text();self.assertIn('if (v == 7 && sm64ds_front_options_begin()) { func_ov007_020c1d8c(0); return; }',wrench)
   self.assertIn('func_ov007_020c1d78(0);',(root/m.FILES[6]).read_text())
   update=(root/m.FILES[0]).read_text()
   for case in (7,9):
    a,b,_=m.block(update,f'    case {case}: {{');chunk=update[a:b]
    self.assertIn('REG16(0x400100a)',chunk);self.assertIn('sm64ds_native_pause_begin',chunk)
    self.assertLess(chunk.index('SetSubBg1Offset'),chunk.index('sm64ds_native_pause_begin'))
   for case in ('8','0xa'):
    a,b,_=m.block(update,f'    case {case}: {{');chunk=update[a:b]
    self.assertIn('sm64ds_native_pause_update();\n        return;\n#else',chunk)
   self.assertEqual(update.count('sm64ds_native_pause_parent(data_0209f248);'),4)
   self.assertNotIn('sm64ds_options_request',update)
   for case in (0,2,4):
    a,b,_=m.block(update,f'    case {case}: {{');chunk=update[a:b]
    self.assertIn('sm64ds_native_pause_restore_parent();',chunk)
   self.assertIn('sm64ds_native_pause_reset();',(root/m.FILES[1]).read_text())
   render=(root/m.FILES[2]).read_text();self.assertLess(render.index('sm64ds_native_pause_render_cursor())'),render.index('unsigned char f248b'))
   self.assertIn('_ZN3OAM21CONTROLLER_MODE_TEXTSE[data_0209f2dc]',render)
   with self.assertRaisesRegex(ValueError,'already patched'):m.patch_sources(root)
 def test_front_close_releases_retail_input_and_can_reopen(self):
  cc=shutil.which('cc')
  if not cc:self.skipTest('C compiler unavailable')
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);self.clone(root);m.patch_sources(root)
   (root/'src/types.h').write_text('typedef unsigned short u16;\n')
   harness=root/'front_input.c'
   harness.write_text(r'''#include <assert.h>
#define SM64DS_PLATFORM_PC 1
#define __3DS__ 1
#include "src/func_ov007_020b7090.c"
static S context;
S *data_ov007_0210342c = &context;
int data_ov007_02103430;
static int blocked, consumed, dispatches, clears, releases, key_resets, open_on_dispatch;
int sm64ds_front_options_blocked(void) { return blocked; }
void func_ov007_020c1d78(int slot) { assert(slot == 0); consumed=0; ++clears; }
void func_ov007_020c20b8(int a, void *b) { (void)a; if (*(int*)b == 0) ++key_resets; }
void func_ov007_020b2e64(u16 *buf) { (void)buf; if (!consumed) ++dispatches; if(open_on_dispatch) { open_on_dispatch=0; blocked=consumed=1; } }
void func_ov007_020c1db0(void *c, u16 *in) { (void)c; if (!in[2]) { assert(in[0]==255 && in[1]==255); ++releases; } }
void *func_ov007_020bdebc(void) { return 0; }
void func_ov007_020aed98(void *p) { (void)p; }
int main(void) {
 for (int cycle=0;cycle<5;++cycle) {
  consumed=1; blocked=1;
  for(int i=0;i<3;++i) func_ov007_020b7090(80,40,1,0,1);
  assert(dispatches==cycle && clears==cycle);
  blocked=0;
  func_ov007_020b7090(80,40,1,0,1);
  assert(!consumed && dispatches==cycle && clears==cycle+1);
  assert(releases==2*(cycle+1) && key_resets==cycle+1);
  func_ov007_020b7090(80,40,1,0,1);
  assert(dispatches==cycle+1);
 }
 // Wrench dispatch opens the modal inside the pump. A fast B close can
 // happen before its next invocation, so there is no blocked entry call.
 consumed=blocked=0;open_on_dispatch=1;
 func_ov007_020b7090(80,40,1,0,1);
 assert(blocked && consumed && dispatches==6);
 blocked=0;
 func_ov007_020b7090(80,40,1,0,1);
 assert(!consumed && clears==6 && dispatches==6 && releases==12 && key_resets==6);
 func_ov007_020b7090(80,40,1,0,1);
 assert(dispatches==7);
 return 0;
}
''')
   binary=root/'front_input'
   subprocess.run([cc,'-std=c11','-fsanitize=address,undefined','-Wno-int-to-void-pointer-cast',str(harness),'-o',str(binary)],check=True,capture_output=True)
   subprocess.run([str(binary)],check=True,capture_output=True)
 def test_no_partial_write_on_bad_render_contract(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);self.clone(root);p=root/m.FILES[2];p.write_text(p.read_text().replace('    unsigned char f248b = data_0209f248;','    unsigned char other = data_0209f248;'))
   before={f:(root/f).read_bytes() for f in m.FILES}
   with self.assertRaisesRegex(ValueError,'contract changed'):m.patch_sources(root)
   self.assertEqual(before,{f:(root/f).read_bytes() for f in m.FILES})
 def test_vendor_and_symlink_refusal(self):
  with self.assertRaisesRegex(ValueError,'vendor'):m.patch_sources(VENDOR)
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);self.clone(root);p=root/m.FILES[1];p.unlink();p.symlink_to(VENDOR/m.FILES[1])
   with self.assertRaisesRegex(ValueError,'escapes'):m.patch_sources(root)
 def test_volume_sprite_generation_is_deterministic(self):
  spec=importlib.util.spec_from_file_location('slider_generator',ROOT/'tools/generate_native_volume_slider.py');generator=importlib.util.module_from_spec(spec);spec.loader.exec_module(generator)
  with tempfile.TemporaryDirectory() as d:
   first=Path(d)/'first.hpp';second=Path(d)/'second.hpp';a=generator.generate(first);b=generator.generate(second)
   self.assertEqual(first.read_bytes(),second.read_bytes());self.assertEqual(first.read_bytes(),(ROOT/'source/integrated_native/options/volume_slider.hpp').read_bytes())
   self.assertEqual(a['header_sha256'],b['header_sha256']);self.assertEqual(a['palette_bank'],1);self.assertEqual(a['copied_game_pixels'],0)
 def test_labels_fit_original_frame(self):
  # Read only font advance metadata from the private extracted ROM, never copy
  # game data into generated tests or public artifacts.
  import struct
  p=VENDOR/'extracted/arm9_dec.bin';rom=ROOT/'build/roms/europe.nds'
  if not p.exists() or not rom.exists():self.skipTest('private font evidence unavailable')
  bank=VENDOR/'tools/message_bank.py';spec=importlib.util.spec_from_file_location('mb',bank);mb=importlib.util.module_from_spec(spec);spec.loader.exec_module(mb)
  base=struct.unpack_from('<I',rom.read_bytes(),0x28)[0];arm=p.read_bytes();off=0x0208f074-base;widths=arm[off:off+256]
  for label in ['Circle Pad Sensitivity: 105','Circle Pad Sensitivity: 200','C-Stick Sensitivity: 105','C-Stick Sensitivity: 200','Full RAM dumps: Disabled','Dump RAM: Disabled','Dump RAM: L R A','Delete dumps','Audio','Sound mode: Surround','Sound mode: Stereo','Resolution: Original','Screen mode: Original','Sound mode: Headphones','Select overlay: Off']:
   with self.subTest(label=label):self.assertLessEqual(sum(widths[c] for c in mb.encode_message(label)[:-1]),144)
if __name__=='__main__':unittest.main()
