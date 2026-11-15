"""Validate the game build seam against the pinned source before staging."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
import shutil
import subprocess
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from prepare_integrated_native_renderer import ROOT,ANCHOR,prepare,runtime_patches,diagnostic_capture_patch,fixed_cadence_patch


class GamePreparerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # The preparer only writes inside build/; a clean checkout has no
        # build/native-renderer yet, and every case makes its folders there.
        (ROOT/'build/native-renderer').mkdir(parents=True,exist_ok=True)

    def test_native_deadlines_survive_slow_render_cycles(self):
        vendor=ROOT/'third_party/sm64ds-3ds/port/platform/3ds'
        clock,cadence=fixed_cadence_patch((vendor/'observed_cycle_clock.h').read_text(),(vendor/'observed_cadence.cpp').read_text())
        with tempfile.TemporaryDirectory(prefix='fixed-native-clock-',dir=ROOT/'build/native-renderer') as temporary:
            folder=Path(temporary);(folder/'clock.hpp').write_text(clock)
            (folder/'test.cpp').write_text('''#include "clock.hpp"
#include <cassert>
int main(){
 // A 45ms image is late at a 60Hz display deadline. The next original
 // game cycle must still target the original timeline, then recover.
 observed_cycle::Clock fixed(60000,60,1,0,0,false),legacy(60000,60,1,0);
 assert(fixed.begin_cycle()&&legacy.begin_cycle());
 assert(fixed.event(1,2700).lateness==1700);
 assert(legacy.event(1,2700).lateness==1700);
 assert(fixed.begin_cycle()&&legacy.begin_cycle());
 assert(fixed.target()==2000&&legacy.target()==3700);
 assert(fixed.event(2,2800).wait==0);
 assert(fixed.begin_cycle());
 assert(fixed.event(3,2900).wait==100);
 assert(fixed.rebases()==0&&fixed.discarded()==0);
 // Alternating slow presentations and cheap discarded images keep the
 // rational original clock, across every outer-loop boundary.
 observed_cycle::Clock c(1000000,60000,1001,0,0,false);
 uint64_t now=0;
 for(uint64_t f=1;f<=10000;++f){
  assert(c.begin_cycle());now+=f%4==1?40000:1000;
  auto d=c.event(f,now);assert(d.valid);
  assert(d.target==f*1000000ull*1001/60000);
  now+=d.wait;
 }
 assert(!c.rebases()&&!c.discarded());
 const auto target=c.target(),frames=c.frames();
 assert(c.postpone(123456));assert(c.target()==target+123456&&c.frames()==frames);
 assert(!c.rebases()&&!c.discarded());
}''')
            subprocess.run([shutil.which('c++'),'-std=c++17',str(folder/'test.cpp'),'-o',str(folder/'test')],check=True,capture_output=True)
            subprocess.run([str(folder/'test')],check=True,capture_output=True)

    def test_native_capture_has_a_drained_owner_and_keeps_requests(self):
        text=''' if(native_diagnostics::requested || native_diagnostics::partial_requested){
  native_diagnostics::requested=native_diagnostics::partial_requested=false;player_present_pipeline_drain();
  const bool capture_ok=native_diagnostics::capture(full_dump,current_scene,updates,anchors,8);
  const bool render_ok=capture_ok && native_diagnostics::capture_render(native_diagnostics::last_capture_stem);
 }
'''
        changed=diagnostic_capture_patch(text)
        self.assertIn('native_capture_ready=!native_capture||sm64ds_fresh_native_diagnostic_begin()',changed)
        self.assertIn('sm64ds_fresh_native_diagnostic_capture(native_diagnostics::last_capture_stem)',changed)
        self.assertIn('if(native_capture)sm64ds_fresh_native_diagnostic_end();',changed)
        self.assertEqual(changed.count('native_diagnostics::requested=native_diagnostics::partial_requested=false;'),1)
        self.assertNotIn('legacy capture unavailable',changed)
        with self.assertRaises(ValueError):diagnostic_capture_patch(changed)
        with self.assertRaises(ValueError):diagnostic_capture_patch(text.replace('player_present_pipeline_drain();','changed();'))

    def run_case(self,text,valid):
        base=ROOT/'build/native-renderer';base.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='game-contract-',dir=base) as temporary:
            output=Path(temporary);(output/'port').mkdir();cmake=output/'port/CMakeLists.txt';cmake.write_text(text)
            catalog=output/'catalog.hpp';catalog.write_text('#pragma once\n#include "model_instance.hpp"\nnamespace sm64ds::render::prepared {\n}\n')
            if valid:
                files=prepare(output,catalog)
                self.assertIn('port/platform/3ds/native_renderer/model_components_hooks.cpp',files)
                self.assertIn('port/platform/3ds/native_renderer/game_materials.cpp',files)
                self.assertIn('port/platform/3ds/native_renderer/texture_sequence_hooks.cpp',files)
                self.assertIn('--wrap=func_020462d0',cmake.read_text())
                self.assertIn('--wrap=func_02046bbc',cmake.read_text())
                self.assertIn('--wrap=func_ov007_020b3f20',cmake.read_text())
                self.assertIn('--wrap=func_ov007_020b3edc',cmake.read_text())
                self.assertIn('--wrap=_ZN15ModelComponents6RenderEP9Matrix4x3P7Vector3',cmake.read_text())
                report=json.loads((output/'native-renderer-overlay.json').read_text())
                self.assertFalse(report['runtime_installed']);self.assertFalse(report['game_renderer_migrated'])
                with self.assertRaises(ValueError):prepare(output,catalog)
            else:
                with self.assertRaises(ValueError):prepare(output,catalog)
                self.assertEqual(cmake.read_text(),text)
                self.assertFalse((output/'port/platform').exists())

    def test_pinned_game_contract(self):
        self.run_case((ROOT/'third_party/sm64ds-3ds/port/CMakeLists.txt').read_text(),True)

    def test_missing_and_duplicated_contracts_are_transactional(self):
        for text in ('cmake_minimum_required(VERSION 3.20)\n',ANCHOR+ANCHOR):
            with self.subTest(text=text):self.run_case(text,False)

    def test_runtime_install_and_changed_boundary_are_transactional(self):
        vendor=ROOT/'third_party/sm64ds-3ds'
        for broken in (False,True):
            with self.subTest(broken=broken),tempfile.TemporaryDirectory(prefix='runtime-contract-',dir=ROOT/'build/native-renderer') as temporary:
                output=Path(temporary)
                for name in ['port/CMakeLists.txt','port/platform/3ds/game_main.cpp','port/platform/3ds/command_drain.cpp','port/platform/3ds/native_mesh_camera.cpp','port/platform/3ds/player_present.cpp','port/ntr/ppu.cpp','port/platform/3ds/game_entry.cpp','port/platform/3ds/observed_cycle_clock.h','port/platform/3ds/observed_cadence.cpp','port/ntr/include/ntr/ppu.h','port/ntr/io.cpp','port/hal/cstd_div.c','port/hal/fs.cpp','port/hal/cxxname_bridge.cpp']:
                    p=output/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((vendor/name).read_bytes())
                if broken:
                    path=output/'port/platform/3ds/command_drain.cpp';path.write_text(path.read_text().replace(' if(ok)gpu_passes::frame_begin();',' changed_boundary();'))
                before={str(p.relative_to(output)):p.read_bytes() for p in output.rglob('*') if p.is_file()}
                catalog=output/'catalog.hpp';catalog.write_text('#pragma once\n#include "model_instance.hpp"\nnamespace sm64ds::render::prepared {\n}\n')
                images=output/'images.hpp';images.write_text('#pragma once\n#include "image_catalog.hpp"\nnamespace sm64ds::render::prepared_images {\n}\n')
                menu=output/'menu.hpp';menu.write_text('#pragma once\n#include "menu_cell_capture.hpp"\nnamespace sm64ds::render::game_hooks::prepared_menu {\n}\n')
                canvas=output/'canvas.hpp';canvas.write_text('#pragma once\n#include "canvas_catalog.hpp"\nnamespace sm64ds::render::prepared_canvases {\n}\n')
                background=output/'background.hpp';background.write_text('#pragma once\n#include "menu_backgrounds.hpp"\nnamespace sm64ds::render::game_hooks::prepared_backgrounds {\n}\n')
                if broken:
                    with self.assertRaises(ValueError):prepare(output,catalog,True,images)
                    self.assertFalse((output/'port/platform/3ds/native_renderer').exists())
                    for name,data in before.items():self.assertEqual((output/name).read_bytes(),data)
                else:
                    prepare(output,catalog,True,images,menu,canvas,background)
                    report=json.loads((output/'native-renderer-overlay.json').read_text())
                    self.assertTrue(report['runtime_installed']);self.assertFalse(report['game_renderer_migrated'])
                    generated=(output/'port/CMakeLists.txt').read_text()
                    self.assertIn('option(SM64DS_FRESH_NATIVE_WRITER_PROBE "Diagnostic source-writer provenance; never a performance build" OFF)',generated)
                    self.assertIn('if(SM64DS_FRESH_NATIVE_WRITER_PROBE)',generated)
                    self.assertIn('SM64DS_NATIVE_IMAGE_CATALOG_HEADER="native_image_catalog.hpp"',generated)
                    self.assertIn('SM64DS_NATIVE_MENU_CATALOG_HEADER="native_menu_cell_catalog.hpp"',generated)
                    self.assertIn('SM64DS_NATIVE_CANVAS_CATALOG_HEADER="native_canvas_catalog.hpp"',generated)
                    self.assertIn('SM64DS_NATIVE_BACKGROUND_CATALOG_HEADER="native_background_catalog.hpp"',generated)
                    for symbol in ['020adc08','020c0a9c','020c0638','020c0288','020b1390']:self.assertIn('--wrap=func_ov007_'+symbol,generated)
                    for symbol in ['func_ov007_020b7138','func_ov007_020bcba8','func_ov007_020bd648','func_ov007_020b7384',
                                   '_ZN3G2x18SetBlendBrightnessEPVtts','_ZN3G2x13SetBlendAlphaEPVttttj',
                                   'func_020551f0','func_ov007_020c897c','func_ov007_020c076c','func_ov007_020c03b0']:
                        self.assertIn('--wrap='+symbol,generated)
                    self.assertEqual(generated.count('platform/3ds/native_renderer/menu_display_state.cpp'),2)
                    for symbol in ['func_0204a4c8','func_0204a17c','func_0204a5c8','func_0204af3c','func_0204be40',
                                   'func_0204bbd8','func_ov007_020b7764','_ZN8Particle10SysTrackerD1Ev','func_020190b8']:
                        self.assertIn('--wrap='+symbol,generated)
                    main=(output/'port/platform/3ds/game_main.cpp').read_text()
                    self.assertIn('else if(sm64ds_fresh_native_diagnostic_hold())present_failures=0;',main)
                    self.assertIn('sm64ds_fresh_native_startup()',main);self.assertIn('sm64ds_fresh_native_begin()',main)
                    self.assertIn('sm64ds_fresh_native_present(',main)
                    self.assertTrue((output/'port/platform/3ds/native_renderer/game_runtime.h').is_file())
                    ppu=(output/'port/ntr/ppu.cpp').read_text()
                    self.assertIn('if((!g_window_cut_scope || !window_cut_enabled()) && !(eng==ENGINE_A&&sm64ds::render::dialogue_window_view::owned.load(std::memory_order_acquire)))return base;',ppu)
                    self.assertIn('dialogue_window_view::control(a-0x04000000u,value,',ppu)
                    # Dry updates: native skips are decided per update by the runtime.
                    self.assertIn('sm64ds_fresh_native_skip_wanted(capturing)',main)
                    # Exact VFP divider/square root and fixed-point divide.
                    io=(output/'port/ntr/io.cpp').read_text();div=(output/'port/hal/cstd_div.c').read_text()
                    self.assertIn('#include "../platform/3ds/native_renderer/ds_math_unit.h"',io)
                    self.assertIn('sm64ds_ds_math_divide(n, d, &quot, &rem)',io);self.assertIn('return sm64ds_ds_math_isqrt64(v);',io)
                    self.assertIn('sm64ds_ds_math_fdiv(a, b)',div)
                    self.assertTrue((output/'port/platform/3ds/native_renderer/ds_math_unit.h').is_file())
                    self.assertIn('do *d++ = *from++; while (--len);',(output/'port/hal/fs.cpp').read_text())
                    # BUILD115: threads stop before libc unmaps the heaps; memory reserve.
                    present=(output/'port/platform/3ds/player_present.cpp').read_text()
                    self.assertIn('extern "C" void sm64ds_present_pipeline_exit_stop(){',present)
                    self.assertIn('if(sm64ds_fatal_handlers_install)sm64ds_fatal_handlers_install();',present)
                    # BUILD124: Old 3DS render worker on core 1 at 80%, mixer on core 0; 30% layout on refusal.
                    self.assertIn('fresh_render_core1=sm64ds_fresh_native_enabled()&&core==1&&sm64ds_fresh_native_render_core1_wanted();',present)
                    self.assertIn('fresh_render_core1=false;limit=APT_SetAppCpuTimeLimit(30);',present)
                    self.assertIn('int audio_core=fresh_render_core1?0:1;',present)
                    self.assertIn('sm64ds_fresh_native_core_layout(fresh_render_core1,audio_core);',present)
                    entry=(output/'port/platform/3ds/game_entry.cpp').read_text()
                    self.assertIn('static void keep_previous_log(const char* path){',entry)
                    self.assertEqual(entry.count('keep_previous_log("'),entry.count('std::freopen('))
                    self.assertIn('if(!cache_way_headroom(sizeof(PreparedCache)+sizeof(BgCandidateCache)))return false;',ppu)
                    self.assertIn('fell back to the game heap',(output/'port/hal/cxxname_bridge.cpp').read_text())

    def test_invalid_image_catalog_is_transactional(self):
        with tempfile.TemporaryDirectory(prefix='image-contract-',dir=ROOT/'build/native-renderer') as temporary:
            output=Path(temporary);(output/'port').mkdir();cmake=output/'port/CMakeLists.txt'
            before=(ROOT/'third_party/sm64ds-3ds/port/CMakeLists.txt').read_text();cmake.write_text(before)
            catalog=output/'catalog.hpp';catalog.write_text('#pragma once\n#include "model_instance.hpp"\nnamespace sm64ds::render::prepared {\n}\n')
            images=output/'images.hpp';images.write_text('unexpected format\n')
            with self.assertRaises(ValueError):prepare(output,catalog,False,images)
            self.assertEqual(cmake.read_text(),before);self.assertFalse((output/'port/platform').exists())

    def test_invalid_menu_catalog_is_transactional(self):
        with tempfile.TemporaryDirectory(prefix='menu-contract-',dir=ROOT/'build/native-renderer') as temporary:
            output=Path(temporary);(output/'port').mkdir();cmake=output/'port/CMakeLists.txt'
            before=(ROOT/'third_party/sm64ds-3ds/port/CMakeLists.txt').read_text();cmake.write_text(before)
            catalog=output/'catalog.hpp';catalog.write_text('#pragma once\n#include "model_instance.hpp"\nnamespace sm64ds::render::prepared {\n}\n')
            menu=output/'menu.hpp';menu.write_text('unexpected format\n')
            with self.assertRaises(ValueError):prepare(output,catalog,False,None,menu)
            self.assertEqual(cmake.read_text(),before);self.assertFalse((output/'port/platform').exists())

    def test_invalid_canvas_catalog_is_transactional(self):
        with tempfile.TemporaryDirectory(prefix='canvas-contract-',dir=ROOT/'build/native-renderer') as temporary:
            output=Path(temporary);(output/'port').mkdir();cmake=output/'port/CMakeLists.txt'
            before=(ROOT/'third_party/sm64ds-3ds/port/CMakeLists.txt').read_text();cmake.write_text(before)
            catalog=output/'catalog.hpp';catalog.write_text('#pragma once\n#include "model_instance.hpp"\nnamespace sm64ds::render::prepared {\n}\n')
            canvas=output/'canvas.hpp';canvas.write_text('unexpected format\n')
            with self.assertRaises(ValueError):prepare(output,catalog,False,None,None,canvas)
            self.assertEqual(cmake.read_text(),before);self.assertFalse((output/'port/platform').exists())

    def test_background_catalog_requires_canvas_transactionally(self):
        with tempfile.TemporaryDirectory(prefix='bg-contract-',dir=ROOT/'build/native-renderer') as temporary:
            output=Path(temporary);(output/'port').mkdir();cmake=output/'port/CMakeLists.txt'
            before=(ROOT/'third_party/sm64ds-3ds/port/CMakeLists.txt').read_text();cmake.write_text(before)
            catalog=output/'catalog.hpp';catalog.write_text('#pragma once\n#include "model_instance.hpp"\nnamespace sm64ds::render::prepared {\n}\n')
            background=output/'background.hpp';background.write_text('#pragma once\n#include "menu_backgrounds.hpp"\nnamespace sm64ds::render::game_hooks::prepared_backgrounds {\n}\n')
            with self.assertRaises(ValueError):prepare(output,catalog,False,None,None,None,background)
            self.assertEqual(cmake.read_text(),before);self.assertFalse((output/'port/platform').exists())


if __name__=='__main__':unittest.main()
