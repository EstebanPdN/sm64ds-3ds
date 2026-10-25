#!/usr/bin/env python3
"""Exercise the real generated UI/camera patches, including late failures."""
import importlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
VENDOR = ROOT / 'third_party/sm64ds-3ds'
sys.path.insert(0, str(ROOT / 'tools'))
camera = importlib.import_module('prepare_integrated_camera')
ui = importlib.import_module('prepare_integrated_native_ui')
display = importlib.import_module('prepare_integrated_display')
pause = importlib.import_module('prepare_integrated_pause_scene')
title = importlib.import_module('prepare_integrated_title_background')
CAMERA = ['src/func_02009e70.cpp', 'src/func_0200bb28.c',
          'src/actors/dMeter_c.cpp', 'src/_ZN5Stage16CheckCameraInputEv.cpp']
RENDER = ['port/platform/3ds/viewer_3ds.cpp', 'port/platform/3ds/gpu_composite.h',
          'port/platform/3ds/gpu_composite.cpp', 'port/platform/3ds/native_obj_gpu.h',
          'port/platform/3ds/native_obj_gpu.cpp', 'port/ntr/ppu.cpp', 'port/CMakeLists.txt',
          'sm64ds-decomp/src/func_ov007_020b2998.c']
VIEWER = RENDER[0]


def copy_files(root, files, base=VENDOR):
    for name in files:
        p = root / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes((base / name).read_bytes())


def snapshot(root):
    return {str(p.relative_to(root)): p.read_bytes() for p in root.rglob('*') if p.is_file()}


def render_fixture(root):
    copy_files(root, RENDER)
    p = root / VIEWER
    text = p.read_text()
    # These two call sites are transformed by render60 before native UI.
    for target in ['g_resident_bottom', 'g_blit_bottom']:
        text = text.replace(f'enhance_draw::bottom({target})', f'enhance_draw::draw({target},0)')
    end = text.index('\n}\n\n// ---- DS fog', text.index('// ---- visual-enhancements: settings'))
    text = text[:end] + '\nstatic void shutdown(){for(unsigned i=0;i<2;++i){if(g_tex_ok[i])C3D_TexDelete(&g_tex[i]);g_tex_ok[i]=false;g_tex_failed[i]=false;g_uploaded[i]=0;}if(g_quads)linearFree(g_quads);g_quads=nullptr;}' + text[end:]
    p.write_text(text)


class PrepareContracts(unittest.TestCase):
    def test_pause_hold_decision_and_target_ownership(self):
        cc = shutil.which('clang++')
        if not cc:
            self.skipTest('C++ compiler unavailable')
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            render_fixture(root)
            display.prepare(root)
            pause.prepare(root)
            viewer = (root / VIEWER).read_text()
            start = viewer.index('    if(resident && g_pause_hold_frame)')
            end = viewer.index('#endif', start)
            body = viewer[start:end]
            frame_begin = viewer.rindex('if(!C3D_FrameBegin(', 0, start)
            self.assertLess(frame_begin, start)
            self.assertLess(end, viewer.index('trim_gpu_textures(0)', end))
            self.assertNotIn('pause_scene.load', body)
            self.assertIn('g_pause_hold_frame=pause_hold;', viewer)
            driver = root / 'hold.cpp'
            driver.write_text('''#include <cassert>
#include <initializer_list>
static bool g_pause_hold_frame,g_pause_held_valid,g_pause_held_stereo,g_native_stereo;
static unsigned draws,stereo_calls;
static void gfxSet3D(bool active){assert(active==g_pause_held_stereo);++stereo_calls;}
static bool render(bool resident){
''' + body + '''
 ++draws;return true;
}
int main(){
 for(bool stereo:{false,true}){
  g_pause_held_valid=true;g_pause_held_stereo=stereo;g_pause_hold_frame=true;
  const auto before=draws;assert(render(true));assert(draws==before);
  assert(g_pause_held_valid && g_native_stereo==stereo);
  g_pause_hold_frame=false;assert(render(true));assert(!g_pause_held_valid);
  g_pause_held_valid=true;assert(render(false));assert(g_pause_held_valid);
 }
 assert(stereo_calls==2);
}
''')
            binary = root / 'hold'
            subprocess.run([cc, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(driver), '-o', str(binary)],
                           check=True, capture_output=True)
            subprocess.run([str(binary)], check=True, capture_output=True)

    def test_actual_ppu_routes_mask_to_physical_lower_only(self):
        cc = shutil.which('clang++')
        if not cc:
            self.skipTest('C++ compiler unavailable')
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / 'platform/3ds').mkdir(parents=True)
            shutil.copyfile(ROOT / 'source/integrated_native/options/native_lower_state.hpp',
                            root / 'platform/3ds/native_lower_state.hpp')
            (root / 'native_ppu.cpp').write_text(ui.ppu_source((VENDOR / 'port/ntr/ppu.cpp').read_text()))
            binary = root / 'routing'
            subprocess.run([cc, '-std=c++17', '-O1', '-g', '-D__3DS__', '-fsanitize=address,undefined',
                            '-I' + str(root), '-I' + str(VENDOR / 'port/ntr/include'),
                            '-I' + str(ROOT / 'source/integrated_native'),
                            str(ROOT / 'tests/options_menu/test_native_ui_routing.cpp'),
                            str(VENDOR / 'port/ntr/vram_banks.cpp'), str(VENDOR / 'port/ntr/bitmap.cpp'),
                            '-o', str(binary)], check=True, capture_output=True)
            subprocess.run([str(binary)], check=True, capture_output=True)

    def test_camera_late_contract_failure_preserves_every_source(self):
        for name in CAMERA:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as d:
                root = Path(d)
                copy_files(root, CAMERA, VENDOR / 'sm64ds-decomp')
                p = root / name
                p.write_text('// changed upstream contract\n')
                before = snapshot(root)
                with self.assertRaises(ValueError):
                    camera.patch_camera_sources(root)
                self.assertEqual(before, snapshot(root))

    def test_camera_duplicate_and_alias_are_rejected_without_writes(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            copy_files(root, CAMERA, VENDOR / 'sm64ds-decomp')
            camera.patch_camera_sources(root)
            before = snapshot(root)
            with self.assertRaisesRegex(ValueError, 'already patched'):
                camera.patch_camera_sources(root)
            self.assertEqual(before, snapshot(root))
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            copy_files(root, CAMERA, VENDOR / 'sm64ds-decomp')
            p = root / CAMERA[-1]
            p.unlink()
            p.symlink_to(VENDOR / 'sm64ds-decomp' / CAMERA[-1])
            before = snapshot(root)
            with self.assertRaisesRegex(ValueError, 'symlink'):
                camera.patch_camera_sources(root)
            self.assertEqual(before, snapshot(root))

    def test_native_camera_disables_all_invisible_touch_actions(self):
        cc = shutil.which('clang++')
        if not cc:
            self.skipTest('C++ compiler unavailable')
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            copy_files(root, CAMERA, VENDOR / 'sm64ds-decomp')
            camera.patch_camera_sources(root)
            (root / 'src/types.h').write_text('typedef unsigned char u8; typedef unsigned short u16;\n')
            harness = root / 'camera.cpp'
            harness.write_text(r'''#include <cassert>
#define SM64DS_PLATFORM_PC 1
#define __3DS__ 1
#include "src/_ZN5Stage16CheckCameraInputEv.cpp"
u8 data_0209f2c4, data_0209f20c, data_0209f294, data_0209d660, data_0209f21c;
u8 data_0209f350[4], data_ov002_02111180;
u16 data_0209f368[4];
TouchInfo data_020a0de8[4]; CamInput data_0209f498[4]; u8* data_0209f318;
int main() {
 for(unsigned mode=0; mode<3; ++mode) for(unsigned y=0; y<192; ++y) for(unsigned x=0; x<256; ++x) {
  data_0209f21c=4; data_ov002_02111180=16;
  for(unsigned i=0;i<4;++i) {
   data_020a0de8[i]={1,1,u8(x),u8(y)};
   data_0209f498[i].f16=u8(mode);data_0209f498[i].held=data_0209f498[i].pressed=0xffff;
   data_0209f350[i]=2; data_0209f368[i]=0x300;
  }
  Stage::CheckCameraInput();
  assert(!data_ov002_02111180);
  for(unsigned i=0;i<4;++i) {
   assert(!data_0209f498[i].held && !data_0209f498[i].pressed);
   assert(!data_0209f350[i] && !data_0209f368[i] && data_0209f498[i].f16==mode);
  }
 }
}
''')
            binary = root / 'camera'
            subprocess.run([cc, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unknown-pragmas', '-fsanitize=address,undefined',
                            str(harness), '-o', str(binary)], check=True, capture_output=True)
            subprocess.run([str(binary)], check=True, capture_output=True)

    def test_all_render_preparers_reject_late_contract_drift_without_writes(self):
        cases = [(ui, RENDER[4]), (display, 'port/CMakeLists.txt'),
                 (pause, VIEWER), (title, RENDER[-1])]
        for module, bad in cases:
            with self.subTest(module=module.__name__), tempfile.TemporaryDirectory() as d:
                root = Path(d)
                render_fixture(root)
                if module in [pause, title]:
                    display.prepare(root)
                if module is title:
                    pause.prepare(root)
                p = root / bad
                if module is display:
                    p.unlink()  # CMake is read after every renderer contract.
                else:
                    p.write_text('// changed late source contract\n')
                before = snapshot(root)
                with self.assertRaises((ValueError, FileNotFoundError)):
                    module.prepare(root)
                self.assertEqual(before, snapshot(root))

    def test_complete_patch_order_duplicate_refusal_and_vendor_hashes(self):
        vendor_before = {p: (VENDOR / p).read_bytes() for p in RENDER}
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            render_fixture(root)
            for module in [display, pause, title, ui]:
                changed = module.prepare(root)
                self.assertTrue(all((root / p).is_file() for p in changed))
                before = snapshot(root)
                with self.assertRaises(ValueError):
                    module.prepare(root)
                self.assertEqual(before, snapshot(root))
            self.assertNotIn('native_camera_gpu.hpp', (root / VIEWER).read_text())
        self.assertEqual(vendor_before, {p: (VENDOR / p).read_bytes() for p in RENDER})

    def test_two_pristine_preparations_produce_identical_patches(self):
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            left, right = Path(a), Path(b)
            for root in [left, right]:
                render_fixture(root)
                for module in [display, pause, title, ui]:
                    module.prepare(root)
                copy_files(root / 'sm64ds-decomp', CAMERA, VENDOR / 'sm64ds-decomp')
                camera.patch_camera_sources(root / 'sm64ds-decomp')
            self.assertEqual(snapshot(left), snapshot(right))

    def test_every_render_preparer_rejects_vendor_and_late_symlink(self):
        for module, bad in [(ui, RENDER[4]), (display, RENDER[2]),
                            (pause, VIEWER), (title, RENDER[-1])]:
            with self.subTest(module=module.__name__):
                with self.assertRaisesRegex(ValueError, 'vendor'):
                    module.prepare(VENDOR)
                with tempfile.TemporaryDirectory() as d:
                    root = Path(d)
                    render_fixture(root)
                    if module in [pause, title]:
                        display.prepare(root)
                    if module is title:
                        pause.prepare(root)
                    p = root / bad
                    p.unlink()
                    p.symlink_to(VENDOR / bad)
                    before = snapshot(root)
                    with self.assertRaisesRegex(ValueError, 'symlink'):
                        module.prepare(root)
                    self.assertEqual(before, snapshot(root))

    def test_display_contracts_remain_enabled_in_optimized_python(self):
        command = "import prepare_integrated_display as d; d.once('changed','old','new')"
        r = subprocess.run([sys.executable, '-O', '-c', command], cwd=ROOT / 'tools', capture_output=True)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn(b'ValueError', r.stderr)


if __name__ == '__main__':
    unittest.main()
