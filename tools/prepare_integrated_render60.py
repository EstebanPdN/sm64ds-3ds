#!/usr/bin/env python3
"""Derive an auditable render-only experiment without editing vendor sources."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]

def once(text,old,new):
    assert text.count(old)==1, 'Reviewed hook changed: '+old[:90]
    return text.replace(old,new,1)

def optimize_inactive_alpha(text):
    text=once(text,'const bool blend1=mode==1;\n    if(blend1){',
        'const bool mode1=mode==1;\n'
        '    // Inactive alpha has no overlay source pixels to blend. Keep the\n'
        '    // original admission and resolved state, but avoid its CPU loop\n'
        '    // and the false overlay-cache dependency on affine motion.\n'
        '    const bool blend1=mode1 && (scene.blend&overlay_mask);\n'
        '    if(mode1){')
    old='PpuCompositionState resolved=scene;if(blend1){resolved.blend=0;resolved.alpha=0;}'
    assert text.count(old)==2
    return text.replace(old,'PpuCompositionState resolved=scene;if(mode1){resolved.blend=0;resolved.alpha=0;}')

def affine_alpha_source(text):
    return optimize_inactive_alpha(text)+'\n'+(ROOT/'source/integrated_native/affine_alpha_objects.inc').read_text()

def prepare(vendor,output,lock,rom_manifest=None):
    output.mkdir(parents=True,exist_ok=True)
    for name,expected in lock['vendored_files_sha256'].items():
        raw=(vendor/name).read_bytes();assert hashlib.sha256(raw).hexdigest()==expected
        target=output/name;target.parent.mkdir(parents=True,exist_ok=True)
        if not target.exists() or target.read_bytes()!=raw:target.write_bytes(raw)
        shutil.copymode(vendor/name,target)
    dependency=output/'sm64ds-decomp'
    if dependency.is_symlink():
        assert dependency.resolve()==(vendor/'sm64ds-decomp').resolve()
        dependency.unlink() # Only this tool's own generated alias.
    if not dependency.exists():
        subprocess.run(['git','clone','--no-hardlinks',str(vendor/'sm64ds-decomp'),str(dependency)],check=True)
        subprocess.run(['git','-C',str(dependency),'checkout','--detach',lock['decomp_commit']],check=True)
    assert subprocess.check_output(['git','-C',str(dependency),'rev-parse','HEAD'],text=True).strip()==lock['decomp_commit']
    # Restore only our previously frozen patches; reject concurrent edits.
    receipt=output/'render60-overlay.json'
    if receipt.exists():
        previous=json.loads(receipt.read_text())
        for name,expected in previous['files'].items():
            if not name.startswith('sm64ds-decomp/src/'):continue
            relative=Path(name).relative_to('sm64ds-decomp')
            assert '..' not in relative.parts
            target=dependency/relative
            pristine=(vendor/'sm64ds-decomp'/relative).read_bytes()
            assert hashlib.sha256(target.read_bytes()).hexdigest() in {expected,hashlib.sha256(pristine).hexdigest()}, 'Concurrent generated decomp edit: '+name
            target.write_bytes(pristine)
    assert not subprocess.check_output(['git','-C',str(dependency),'status','--porcelain','--untracked-files=no'],text=True).strip()
    shutil.copyfile(ROOT/'source/integrated_native/render_60.hpp',output/'port/platform/3ds/render_60.hpp')
    shutil.copyfile(ROOT/'source/integrated_native/native_diagnostics.hpp',output/'port/platform/3ds/native_diagnostics.hpp')
    shutil.copyfile(ROOT/'source/integrated_native/affine_overlay_key.hpp',output/'port/platform/3ds/affine_overlay_key.hpp')
    path=output/'port/platform/3ds/player_present.cpp';text=path.read_text()
    text='#include "render_60.hpp"\n#include "observed_cadence.h"\n'+text
    text='extern "C" unsigned sm64ds_render60_source_epoch() __attribute__((weak));\n'+text
    text='extern "C" unsigned sm64ds_render60_scene() __attribute__((weak));\n'+text
    text='extern "C" unsigned sm64ds_profile_context() __attribute__((weak));\n'+text
    text=once(text,'#undef u32\n#endif', '#undef u32\n#endif\n#include "native_diagnostics.hpp"')
    text=once(text,'else job.b.emplace(ntr::ppu_prepare_candidates_cached(ntr::ENGINE_B));',
        'else if(auto affine=ntr::ppu_prepare_affine_alpha_objects(ntr::ENGINE_B))job.b.emplace(*affine);\n'
        '    else job.b.emplace(ntr::ppu_prepare_candidates_cached(ntr::ENGINE_B));')
    text=once(text,'if(!mesh_job_pending)return;',
        'if(!mesh_job_pending)return;\n'
        '    native_diagnostics::trace.record(ntr::rt_frame(),sm64ds_render60_source_epoch?sm64ds_render60_source_epoch():0,sm64ds_render60_scene?sm64ds_render60_scene():0,sm64ds_profile_context?sm64ds_profile_context():0);')
    text=once(text,'if(same){++held_skips;return true;}',
        'if(same && !render60::interpolation.pending()){++held_skips;return true;}')
    text=once(text,'job.mesh=frame;job.mesh_compiled=bool(compiled);',
        'render60::jobs.report(native_diagnostics::verbose());\n'
        '    job.mesh=compiled?render60::interpolation.select(frame,!observed_cadence::behind(),ntr::rt_frame(),sm64ds_render60_source_epoch?sm64ds_render60_source_epoch():0):frame;job.mesh_compiled=bool(compiled);\n'
        '    job.render60_selection=compiled?render60::interpolation.selection():render60::Selection::Source;')
    text=once(text,'struct Job {\n','struct Job {\n    render60::Selection render60_selection=render60::Selection::Source;\n')
    text=once(text,'if(job.mesh_compiled)bmd_compiled_gpu_result(job.mesh,false,ok);',
        'if(job.mesh_compiled){bmd_compiled_gpu_result(job.mesh,false,ok);\n'
        '        render60::jobs.completed(ok,svcGetSystemTick(),job.render60_selection);}')
    # A camera state cut is also a temporal cut. The call executes on the game
    # thread before publication; the worker never reads interpolation state.
    text+='\nextern "C" void sm64ds_render60_cut(){render60::interpolation.reset();}\n'
    text+='extern "C" void sm64ds_render60_actor_order(const uint64_t* ids,unsigned count){render60::interpolation.observe_owners(ids,count);}\n'
    text+='extern "C" bool sm64ds_render60_need_exact(){return render60::interpolation.pending();}\n'
    path.write_text(text)
    path=output/'port/platform/3ds/native_mesh_camera.cpp';text=path.read_text()
    text=text.replace('ntr::gx_native_mesh_camera_cut();','render60_camera_cut();')
    text=once(text,'#include <cstdio>','#include <cstdio>\n'
        'extern "C" void sm64ds_render60_cut();\n'
        'static void render60_camera_cut(){ntr::gx_native_mesh_camera_cut();sm64ds_render60_cut();}')
    path.write_text(text)
    path=output/'port/platform/3ds/game_main.cpp';text=path.read_text()
    text=once(text,'#undef u32\n', '#undef u32\n#include "native_diagnostics.hpp"\n')
    text='extern "C" void sm64ds_render60_cut();\nextern "C" bool sm64ds_render60_need_exact();\n'+text
    text=once(text,'native_mesh_driver_reset();ntr::gx_native_mesh_camera_cut();',
        'sm64ds_render60_cut();native_mesh_driver_reset();ntr::gx_native_mesh_camera_cut();')
    text=once(text,'static unsigned updates;',
        'static unsigned updates;\nextern "C" unsigned sm64ds_render60_source_epoch(){return updates;}\n'
        'extern "C" unsigned sm64ds_render60_scene(){return current_scene;}\n'
        'extern "C" unsigned char data_0208751c[],data_0208ee44[],data_0209f2c4;')
    text=once(text,'static bool frame_hook(uint64_t frame){',
        '// Diagnostic context is sampled on the game thread after the worker drains.\n'
        '// Signature distinguishes known flags from older zero-reserved V2 traces.\n'
        'extern "C" unsigned sm64ds_profile_context(){return 0x534d0000u |\n'
        ' unsigned(current_scene==3 && data_0209f2c4!=0) |\n'
        ' (unsigned(current_scene==3 && data_0209d660!=0)<<1) |\n'
        ' (unsigned(current_scene==3 && data_0209fc48!=0)<<2);}\n'
        'static bool frame_hook(uint64_t frame){')
    text=once(text,'require_runtime(sm64ds_menu_poll_input(),"local HID transfer");',
        'require_runtime(sm64ds_menu_poll_input(),"local HID transfer");\n'
        ' // Developer chords; L has no game function (enhance_runtime filters it).\n'
        ' // L+R+A full dump, L+R+B partial dump without RAM, L+R+X delete dumps.\n'
        ' {const uint32_t held=hidKeysHeld(),down=hidKeysDown(),lr=KEY_L|KEY_R;\n'
        '  if((held&lr)==lr && (down&(lr|KEY_A|KEY_B|KEY_X))){\n'
        '   if((held&KEY_A) && native_diagnostics::enabled())native_diagnostics::requested=true;\n'
        '   else if(held&KEY_B)native_diagnostics::partial_requested=true;\n'
        '   else if(held&KEY_X)native_diagnostics::delete_requested=true;\n'
        '  }}')
    text=once(text,'static bool frame_hook(uint64_t frame){',
        'static bool frame_hook(uint64_t frame){\n'
        ' enhance::live().pause_scene.store(current_scene==3 && data_0209f2c4!=0,std::memory_order_release);\n'
        ' static bool verbose_checked=false;\n'
        ' if(!verbose_checked){verbose_checked=true;native_diagnostics::configure_verbose();}\n'
        ' if(native_diagnostics::verbose() && current_scene==1 && frame%30==0 && frame<1800){\n'
        '  std::printf("Title MMIO frame=%llu disp=%08x/%08x blend=%04x/%04x bldy=%u/%u master=%04x/%04x clear=%08x palette=%04x/%04x\\n",(unsigned long long)frame,\n'
        '   unsigned(ntr::io_read(0x04000000,4)),unsigned(ntr::io_read(0x04001000,4)),\n'
        '   unsigned(ntr::io_read(0x04000050,2)),unsigned(ntr::io_read(0x04001050,2)),\n'
        '   unsigned(ntr::io_read(0x04000054,2)),unsigned(ntr::io_read(0x04001054,2)),\n'
        '   unsigned(ntr::io_read(0x0400006c,2)),unsigned(ntr::io_read(0x0400106c,2)),\n'
        '   unsigned(ntr::io_read(0x04000350,4)),unsigned(ntr::io_read(0x05000000,2)),unsigned(ntr::io_read(0x05000400,2)));\n'
        ' }\n'
        ' if(native_diagnostics::enabled() && current_scene==1 && frame>=900 && frame<1800){\n'
        '  const char* once="sdmc:/3ds/sm64ds/native-title-capture.once";\n'
        '  if(FILE* f=std::fopen(once,"rb")){std::fclose(f);if(std::remove(once)==0)native_diagnostics::requested=true;}\n'
        ' }')
    text=once(text,'observed_cadence::after_frame(frame);',
        'observed_cadence::after_frame(frame);\n'
        ' static bool benchmark_model_checked=false,benchmark_new=false;\n'
        ' if(!benchmark_model_checked){APT_CheckNew3DS(&benchmark_new);benchmark_model_checked=true;}\n'
        ' const bool benchmark_capture=native_benchmark::frame(current_scene,updates,native_diagnostics::enabled(),benchmark_new,svcGetSystemTick());\n'
        ' if(benchmark_capture)native_diagnostics::requested=true;\n'
        ' const uint32_t pause_requests=sm64ds_native_pause_take_requests();\n'
        ' if(pause_requests&1u){const bool saved=enhance::save_file(enhance::kSettingsPath,enhance::snapshot());std::printf("Pause settings saved=%u\\n",unsigned(saved));}\n'
        ' if((pause_requests&2u) && native_diagnostics::enabled())native_diagnostics::requested=true;\n'
        ' if(pause_requests&4u)native_diagnostics::delete_requested=true;\n'
        ' if(native_diagnostics::delete_requested){\n'
        '  native_diagnostics::delete_requested=false;player_present_pipeline_drain();\n'
        '  const auto result=native_diagnostics::delete_dumps();\n'
        '  std::printf("Diagnostic deletion removed=%u failed=%u\\n",result.removed,result.failed);\n'
        ' }\n'
        ' if(native_diagnostics::requested || native_diagnostics::partial_requested){\n'
        '  const uint64_t diagnostic_begin=svcGetSystemTick();\n'
        '  const bool full_dump=native_diagnostics::requested;\n'
        '  native_diagnostics::requested=native_diagnostics::partial_requested=false;player_present_pipeline_drain();\n'
        '  const native_diagnostics::Anchor anchors[]={\n'
        '   {"sm64ds_render60_source_epoch",reinterpret_cast<const void*>(&sm64ds_render60_source_epoch)},\n'
        '   {"ds_io",ntr::host_ptr(0x04000000)},{"ds_palette",ntr::host_ptr(0x05000000)},\n'
        '   {"data_0208751c",data_0208751c},{"data_0208ee44",data_0208ee44},\n'
        '   {"data_0209f394",data_0209f394},{"data_0209f318",&data_0209f318},\n'
        '   {"render60_interpolation",&render60::interpolation}};\n'
        '  const bool capture_ok=native_diagnostics::capture(full_dump,current_scene,updates,anchors,sizeof(anchors)/sizeof(anchors[0]));\n'
        '  const bool render_ok=capture_ok && native_diagnostics::capture_render(native_diagnostics::last_capture_stem);\n'
        '  native_diagnostics::dump_progress(100,true,!(capture_ok && render_ok));\n'
        '  if(benchmark_capture)native_benchmark::capture_completed(capture_ok && render_ok,svcGetSystemTick());\n'
        '  else native_benchmark::external_capture_completed(diagnostic_begin,svcGetSystemTick());\n'
        ' }')
    text='#include "render_60.hpp"\n#include "diagnostic_files.hpp"\n#include "render_diagnostic.hpp"\n#include "native_benchmark.h"\n#include "native_pause_menu.h"\n'+text
    text=once(text,'const bool skip_present=!capturing && consecutive_skips<3',
        'const bool skip_present=!capturing && !sm64ds_render60_need_exact() && consecutive_skips<3')
    text+='''
// Read-only render-list identity admission. A topology reused by two identical
// actors is insufficient: retain actual owner addresses and their unique IDs.
extern "C" unsigned data_020a4b98[];
extern "C" void* __real_func_02043fdc(void*);
extern "C" void sm64ds_render60_actor_order(const uint64_t*,unsigned);
extern "C" { uint64_t sm64ds_render60_current_actor=0; }
// Same native member-function-pointer dispatch as func_02043fdc. Instrument
// only the render list; every other process list calls the original routine.
class Render60Object {public:int dummy;};
using Render60Callback=void(Render60Object::*)();
struct Render60Node {int pad;Render60Node* next;Render60Object* object;};
struct Render60List {Render60Node* head;int pad;Render60Callback callback;};
extern "C" Render60Node* data_020a4b68;
extern "C" void* __wrap_func_02043fdc(void* list){
 if(list==data_020a4b98){
  auto* typed=static_cast<Render60List*>(list);
  if(typed->callback==nullptr)return reinterpret_cast<void*>(1);
  uint64_t ids[512];unsigned count=0;
  auto* node=*static_cast<unsigned**>(list);
  while(node && count<512){
   auto* actor=reinterpret_cast<const unsigned*>(node[2]);
   if(!actor){count=513;break;}
   ids[count++]=(uint64_t(actor[1])<<32)|node[2];
   node=reinterpret_cast<unsigned*>(node[1]);
  }
  if(node)count=513;
  sm64ds_render60_actor_order(ids,count);
  for(auto* current=typed->head;current;){
   data_020a4b68=current;auto* next=current->next;
   const auto* actor=reinterpret_cast<const unsigned*>(current->object);
   sm64ds_render60_current_actor=(uint64_t(actor[1])<<32)|uint32_t(uintptr_t(actor));
   (current->object->*typed->callback)();
   sm64ds_render60_current_actor=0;current=next;
  }
  data_020a4b68=nullptr;return reinterpret_cast<void*>(1);
 }
 return __real_func_02043fdc(list);
}
'''
    path.write_text(text)
    path=output/'port/platform/3ds/observed_cadence.cpp';text=path.read_text()
    text=once(text,'if(enabled)setenv("PLAYER_RESIDENT_NO_VBLANK_WAIT","1",1);',
        '// Render60: standard Citro3D LCD synchronization prevents two queued\n'
        '    // images from replacing each other before the screen consumes them.\n'
        '    if(enabled)unsetenv("PLAYER_RESIDENT_NO_VBLANK_WAIT");')
    path.write_text(text)
    path=output/'port/platform/3ds/prepared_mesh_view.h';text=path.read_text()
    text=once(text,'struct Instance {\n','struct Instance {\n    uint64_t temporal_owner=0;\n')
    text=once(text,'struct Frame {\n','struct Frame {\n    HostVector<uint64_t> temporal_owners;\n')
    text=once(text,'frame->attributes.reserve(vertices);frame->matrices.reserve(matrix_count);',
        'frame->attributes.reserve(vertices);frame->matrices.reserve(matrix_count);\n'
        '        frame->temporal_owners.reserve(input.size());')
    text=once(text,'for(const auto& instance:input){\n            if(any_lit){',
        'for(const auto& instance:input){\n            frame->temporal_owners.push_back(instance.temporal_owner);\n            if(any_lit){')
    path.write_text(text)
    path=output/'port/ntr/bmd_sdk_bridge.inc';text=path.read_text()
    text='extern "C" unsigned long long sm64ds_render60_current_actor __attribute__((weak));\n'+text
    text=once(text,'if(!store.append_reserved(std::move(active->evaluator.instance))){',
        'if(&sm64ds_render60_current_actor)active->evaluator.instance.temporal_owner=sm64ds_render60_current_actor;\n'
        '        if(!store.append_reserved(std::move(active->evaluator.instance))){')
    path.write_text(text)
    path=output/'port/ntr/ppu.cpp';path.write_text(affine_alpha_source(path.read_text()))
    path=output/'port/ntr/include/ntr/ppu.h';text=path.read_text()
    text=once(text,'PpuPreparedCandidates ppu_prepare_candidates_cached(Engine eng);',
        'PpuPreparedCandidates ppu_prepare_candidates_cached(Engine eng);\n'
        '// Experimental Engine B only: CPU-resolved semi OBJ over GPU-affine BG.\n'
        '// Call once after wait_idle and helper-B join; borrowed rasters remain\n'
        '// valid only until the next engine-B preparation. Refusal keeps full PPU.\n'
        'std::optional<PpuPreparedCandidates> ppu_prepare_affine_alpha_objects(Engine eng);')
    path.write_text(text)
    path=output/'port/platform/3ds/gpu_composite.h';text=path.read_text()
    text=once(text,'#pragma once','#pragma once\n#include "affine_overlay_key.hpp"')
    text=once(text,'uint64_t affine_overlay_generation_=0;','render60::AffineOverlayKey affine_overlay_key_;')
    path.write_text(text)
    path=output/'port/platform/3ds/gpu_composite.cpp';text=path.read_text()
    text=once(text,'affine_overlay_generation_=0;','affine_overlay_key_.reset();')
    text=once(text,'if(affine_overlay_generation_!=layer.overlay_generation){',
        'if(!affine_overlay_key_.matches(layer.overlay,layer.overlay_generation)){')
    text=once(text,'affine_overlay_generation_=layer.overlay_generation;',
        'affine_overlay_key_.commit(layer.overlay,layer.overlay_generation);')
    path.write_text(text)
    path=output/'build3ds/build_game.py';text=path.read_text()
    text=once(text,"'memcmp']:","'memcmp','func_02043fdc']:")
    path.write_text(text)
    rom_changes=[]
    if rom_manifest is not None:
        for name in ['boot_allocator.hpp','rom_bootstrap.hpp','rom_discovery.hpp','rom_setup_3ds.hpp','dsp_bootstrap.hpp',
                     'loading_art.hpp','loading_locale.hpp','rom_art.hpp','loading_screen.hpp','loading_screen_3ds.hpp',
                     'packed_assets.hpp','packed_assets_3ds.hpp','packed_builder.hpp','setup_power.hpp']:
            shutil.copyfile(ROOT/'source/integrated_native'/name,output/'port/platform/3ds'/name)
            rom_changes.append('port/platform/3ds/'+name)
        shutil.copyfile(rom_manifest,output/'port/platform/3ds/rom_install_manifest.hpp')
        rom_changes.append('port/platform/3ds/rom_install_manifest.hpp')
        shutil.copyfile(rom_manifest.parent/'rom_art_embedded.hpp',output/'port/platform/3ds/rom_art_embedded.hpp')
        rom_changes.append('port/platform/3ds/rom_art_embedded.hpp')
        shutil.copyfile(rom_manifest.parent/'rom_art_manifest.hpp',output/'port/platform/3ds/rom_art_manifest.hpp')
        rom_changes.append('port/platform/3ds/rom_art_manifest.hpp')
        shutil.copyfile(rom_manifest.parent/'loading_style.hpp',output/'port/platform/3ds/loading_style.hpp')
        rom_changes.append('port/platform/3ds/loading_style.hpp')
        path=output/'port/platform/3ds/game_entry.cpp';text=path.read_text()
        text=once(text,'#include "enhance_settings.h"',
            '#include "enhance_settings.h"\n#include "rom_setup_3ds.hpp"\n#include <sys/stat.h>')
        text=once(text,' chdir("sdmc:/sm64ds");',
            ' ::mkdir("sdmc:/3ds",0777);\n ::mkdir("sdmc:/3ds/sm64ds",0777);\n chdir("sdmc:/3ds/sm64ds");')
        text=once(text,'gfxInitDefault();sm64ds_configure_cpu_mode();',
            'gfxInitDefault();sm64ds_configure_cpu_mode();\n'
            ' if(!rom_setup_3ds::prepare()){gfxExit();return 1;}\n'
            ' rom_setup_3ds::log_memory("loading-resources-released");')
        text=once(text,'std::fflush(stdout);gfxExit();return result;',
            'std::fflush(stdout);packed_assets::unmount();dsp_bootstrap::release_after_audio_shutdown();gfxExit();return result;')
        path.write_text(text);rom_changes.append('port/platform/3ds/game_entry.cpp')
        path=output/'port/platform/3ds/enhance_settings.h';text=path.read_text()
        text=once(text,'uint8_t camera_cstick = 1;','uint8_t camera_cstick = 2;')
        text=once(text,'std::atomic<uint8_t> camera_cstick{1};','std::atomic<uint8_t> camera_cstick{2};')
        path.write_text(text);rom_changes.append('port/platform/3ds/enhance_settings.h')
    changed=['port/platform/3ds/'+name for name in
        ['render_60.hpp','native_diagnostics.hpp','player_present.cpp','native_mesh_camera.cpp','game_main.cpp','prepared_mesh_view.h','observed_cadence.cpp']]
    changed.append('port/ntr/bmd_sdk_bridge.inc')
    changed.append('port/ntr/ppu.cpp')
    changed.append('port/ntr/include/ntr/ppu.h')
    changed.extend('port/platform/3ds/'+n for n in ['affine_overlay_key.hpp','gpu_composite.h','gpu_composite.cpp'])
    changed.append('build3ds/build_game.py')
    if rom_manifest is not None:
        # Adapt runtime paths only in the generated overlay; preserve vendor pins.
        for path in (output/'port').rglob('*'):
            if not path.is_file() or path.suffix not in {'.cpp','.h','.hpp','.inc'}:continue
            text=path.read_text()
            if 'sdmc:/sm64ds' not in text:continue
            path.write_text(text.replace('sdmc:/sm64ds','sdmc:/3ds/sm64ds'))
            rom_changes.append(path.relative_to(output).as_posix())
    from prepare_integrated_camera import patch_camera_sources
    from prepare_integrated_display import prepare as prepare_display
    from prepare_integrated_native_pause import patch_sources as patch_native_pause
    for path in sorted((ROOT/'source/integrated_native/options').iterdir()):
        if path.suffix not in {'.cpp','.h','.hpp'}:continue
        shutil.copyfile(path,output/'port/platform/3ds'/path.name)
        changed.append('port/platform/3ds/'+path.name)
    art=output/'port/platform/3ds/options_art.hpp'
    subprocess.run([__import__('sys').executable,str(ROOT/'tools/generate_integrated_options_art.py'),
        '--extracted',str(vendor/'sm64ds-decomp/extracted'),'--output',str(art)],check=True)
    changed.append('port/platform/3ds/options_art.hpp')
    patch_camera_sources(dependency)
    camera_names=['src/func_02009e70.cpp','src/func_0200bb28.c','src/actors/dMeter_c.cpp','src/_ZN5Stage16CheckCameraInputEv.cpp']
    option_names=patch_native_pause(dependency)
    intro_path=dependency/'src/StartIntroCutscene.c'
    intro='#ifdef __cplusplus\nextern \"C\" void sm64ds_intro_skip_begin(void);\n#else\nextern void sm64ds_intro_skip_begin(void);\n#endif\n'+intro_path.read_text()
    intro=once(intro,'    RunKuppaScript(&data_020890a0);',
        '    RunKuppaScript(&data_020890a0);\n#if defined(SM64DS_PLATFORM_PC) && defined(__3DS__)\n    sm64ds_intro_skip_begin();\n#endif')
    intro_path.write_text(intro)
    option_names.append('src/StartIntroCutscene.c')
    changed.extend('sm64ds-decomp/'+str(n) for n in camera_names+option_names)
    path=output/'port/CMakeLists.txt';text=path.read_text()
    text=once(text,'target_sources(smoke_player PRIVATE platform/3ds/cstick_camera.cpp)',
        'target_sources(smoke_player PRIVATE platform/3ds/cstick_camera.cpp platform/3ds/native_pause_menu.cpp)')
    path.write_text(text);changed.append('port/CMakeLists.txt')
    from prepare_integrated_benchmark import prepare as prepare_benchmark
    changed.extend(prepare_benchmark(output))
    cmake=output/'port/CMakeLists.txt'
    cmake.write_text(cmake.read_text()+'\ntarget_sources(smoke_player PRIVATE platform/3ds/intro_skip.cpp)\n')
    changed.extend(prepare_display(output))
    from prepare_integrated_pause_scene import prepare as prepare_pause_scene
    prepare_pause_scene(output)
    from prepare_integrated_draw_material import prepare as prepare_draw_material
    changed.extend(prepare_draw_material(output))
    from prepare_integrated_mesh_uniforms import prepare as prepare_mesh_uniforms
    changed.extend(prepare_mesh_uniforms(output))
    from prepare_integrated_vertex_inputs import prepare as prepare_vertex_inputs
    changed.extend(prepare_vertex_inputs(output))
    from prepare_integrated_title_background import prepare as prepare_title_background
    changed.extend(prepare_title_background(output))
    # Apply the saved master gain to completed PCM blocks, before DSP ownership.
    path=output/'port/audio/sound_output_3ds.cpp';text=path.read_text()
    text='#include "../platform/3ds/enhance_settings.h"\n#include "../platform/3ds/audio_gain.hpp"\n'+text
    text=once(text,'        DSP_FlushDataCache(buffer.data_pcm16,Frames*2*sizeof(s16));',
        '        const unsigned volume=enhance::live().audio_volume.load(std::memory_order_relaxed);\n'
        '        if(volume!=100)for(unsigned sample=0;sample<Frames*2;++sample)\n'
        '            buffer.data_pcm16[sample]=enhance::audio_gain(buffer.data_pcm16[sample],volume);\n'
        '        DSP_FlushDataCache(buffer.data_pcm16,Frames*2*sizeof(s16));')
    path.write_text(text);changed.append('port/audio/sound_output_3ds.cpp')
    from prepare_integrated_audio import prepare as prepare_audio
    changed.extend(prepare_audio(output))
    from prepare_integrated_save import prepare as prepare_save
    changed.extend(prepare_save(output))
    # Draw the FPS strip on each upper eye, options on the lower LCD.
    # Observe the actual queue boundaries without splitting, reordering or
    # removing synchronization. Slots41..44 are unused by the pinned backend.
    path=output/'port/platform/3ds/command_drain.cpp';text=path.read_text()
    text='#include "frame_profile.h"\n'+text
    text=once(text,' const bool ok=__real_C3D_FrameBegin(flags);',
        ' bool ok;\n {frame_profile::Scope queue_scope((flags&C3D_FRAME_SYNCDRAW)?41:42);\n'
        '  ok=__real_C3D_FrameBegin(flags);}')
    text=once(text,' __real_C3D_FrameEnd(flags);',
        ' {frame_profile::Scope queue_scope((flags&GX_CMDLIST_FLUSH)?43:44);\n'
        '  __real_C3D_FrameEnd(flags);}')
    path.write_text(text);changed.append('port/platform/3ds/command_drain.cpp')
    path=output/'port/platform/3ds/viewer_3ds.cpp';text=path.read_text()
    # The upstream shadow experiment otherwise schedules up to12 synchronous
    # GPU readbacks, allocating/scanning a full target every600 native frames.
    # It also changes the shadow path on those probe frames. Production must
    # opt into that experiment explicitly; full developer RAM dumps stay intact.
    text=once(text,'const char* v=std::getenv("SM64DS_SHADOW_TARGET_PROBE");return !(v&&v[0]==\'0\');',
        'const char* v=std::getenv("SM64DS_SHADOW_TARGET_PROBE");return v&&v[0]==\'1\';')
    text=once(text,'                        const uint32_t* lower,bool stereo){',
        '                        const uint32_t* lower,bool stereo){\n'
        '    enhance::live().actual_render_width.store(256,std::memory_order_relaxed);\n'
        '    enhance::live().actual_render_height.store(192,std::memory_order_relaxed);\n'
        '    enhance::live().native_fallback.store(enhance::live().res3d.load(std::memory_order_relaxed)==1,std::memory_order_relaxed);')
    text=once(text,'static void bottom(C3D_RenderTarget* target){','static void draw(C3D_RenderTarget* target,unsigned selected){')
    text=text.replace('for(unsigned kind=0;kind<2;++kind){','for(unsigned kind=selected;kind<selected+1;++kind){')
    text=once(text,'-1+8.f/320.f,1-8.f/240.f,-1+520.f/320.f,1-40.f/240.f',
        '1-520.f/400.f,1-52.f/240.f,1-8.f/400.f,1-84.f/240.f')
    text=once(text,'enhance_draw::bottom(g_resident_bottom);','enhance_draw::draw(g_resident_bottom,0);enhance_draw::draw(g_left,1);if(g_native_stereo)enhance_draw::draw(g_right,1);')
    text=once(text,'enhance_draw::bottom(g_blit_bottom);','enhance_draw::draw(g_blit_bottom,0);enhance_draw::draw(g_left,1);if(stereo)enhance_draw::draw(g_right,1);')
    text='namespace enhance_draw {static void shutdown();}\n'+text
    text=once(text,'    C3D_Fini();','    enhance_draw::shutdown();\n    C3D_Fini();')
    insertion=text.index('\n}\n\n// ---- DS fog',text.index('namespace enhance_draw {',text.index('// ---- visual-enhancements: settings')))
    text=text[:insertion]+"\nstatic void shutdown(){for(unsigned i=0;i<2;++i){if(g_tex_ok[i])C3D_TexDelete(&g_tex[i]);g_tex_ok[i]=false;g_tex_failed[i]=false;g_uploaded[i]=0;}if(g_quads)linearFree(g_quads);g_quads=nullptr;}"+text[insertion:]
    path.write_text(text)
    text+='''
// Diagnostic only: synchronize the final target rather than reading the next
// CPU draw buffer. Called after presentation drains, outside an active frame.
bool v3ds_diagnostic_final_top(uint32_t* output){
    if(!output || !g_left || g_left->frameBuf.width!=240 || g_left->frameBuf.height!=400)return false;
    auto* pixels=static_cast<u32*>(linearAlloc(400*240*4));
    if(!pixels)return false;
    const u32 flags=GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
    C3D_SyncDisplayTransfer(static_cast<u32*>(g_left->frameBuf.colorBuf),GX_BUFFER_DIM(240,400),
        pixels,GX_BUFFER_DIM(240,400),flags);
    GSPGPU_InvalidateDataCache(pixels,400*240*4);
    for(unsigned y=0;y<240;++y)for(unsigned x=0;x<400;++x)
        output[y*400+x]=0xff000000u|(pixels[x*240+239-y]>>8);
    linearFree(pixels);return true;
}
'''
    # Normal play still updates on-screen FPS, but periodic printf reaches an
    # unbuffered SD log. Keep error/startup/action logs and gate only telemetry.
    text=once(text,'if (!g_auto || ++frames % 60 == 0)',
        'if (!g_auto || (++frames % 60 == 0 && native_diagnostics::verbose()))')
    for prefix in ('Resident OBJ actual engine=','Resident GPU parts ms:'):
        text=once(text,'std::printf("'+prefix,'if(native_diagnostics::verbose())std::printf("'+prefix)
    path.write_text(text)
    path=output/'port/platform/3ds/game_main.cpp';text=path.read_text()
    text=once(text,'if(frame%120==0)std::printf("Game vblank=',
        'if(native_diagnostics::verbose() && frame%120==0)std::printf("Game vblank=')
    path.write_text(text)
    path=output/'port/platform/3ds/game_entry.cpp';text=path.read_text()
    text=once(text,'#include "enhance_settings.h"','#include "enhance_settings.h"\n#include "enhance_runtime.h"\n#include "build_version.hpp"')
    text=once(text,' enhance::boot_apply();',' std::printf("SM64DS Native %s\\n",sm64ds_build::label);\n enhance::boot_apply();')
    text=once(text,'std::fflush(stdout);','enhance_runtime::shutdown();std::fflush(stdout);')
    path.write_text(text);changed.append('port/platform/3ds/game_entry.cpp')
    path=output/'port/menu/input.cpp';text=path.read_text()
    text='extern "C" int sm64ds_front_options_blocked() __attribute__((weak));\n'+text
    text=once(text,' sample[3]=0; // libctru already supplies calibrated coordinates.',
        ' sample[3]=0; // libctru already supplies calibrated coordinates.\n if(sm64ds_front_options_blocked&&sm64ds_front_options_blocked()){sample[0]=sample[1]=0xffff;sample[2]=0;}')
    text=once(text,' ds_pen_record(touch);\n return sm64ds_local_input_publish',
        ' ds_pen_record(touch);\n if(!touch_allowed){touch[0]=touch[1]=touch[2]=0;}\n return sm64ds_local_input_publish')
    path.write_text(text);changed.append('port/menu/input.cpp')
    from prepare_integrated_native_ui import prepare as prepare_native_ui
    changed.extend(prepare_native_ui(output))
    # Every CPU image lease ends on this worker, even when texture/quad
    # allocation fails or the same generation was already uploaded.
    path=output/'port/platform/3ds/viewer_3ds.cpp';text=path.read_text()
    text=once(text,'    if(!any)return;\n    if(!g_quads){',
        '    struct OverlayReadLease {\n'
        '        const uint32_t* const* images;const uint32_t* generations;\n'
        '        ~OverlayReadLease(){for(unsigned i=0;i<2;++i)if(images[i])\n'
        '            enhance_runtime::acknowledge(i,generations[i]);}\n'
        '    } lease{images,generations};\n'
        '    if(!any)return;\n    if(!g_quads){')
    path.write_text(text)
    from prepare_integrated_collision import prepare as prepare_collision
    changed.extend(prepare_collision(output))
    from prepare_integrated_movement import prepare as prepare_movement
    changed.extend(prepare_movement(output))
    from prepare_integrated_profiling import prepare as prepare_profiling
    changed.extend(prepare_profiling(output))
    from prepare_integrated_runtime_costs import prepare as prepare_runtime_costs
    changed.extend(prepare_runtime_costs(output))
    from prepare_integrated_frame_lock import prepare as prepare_frame_lock
    changed.extend(prepare_frame_lock(output))
    from prepare_integrated_memory import prepare as prepare_memory
    changed.extend(prepare_memory(output))
    from prepare_integrated_sprite_overlay import prepare as prepare_sprite_overlay
    changed.extend(prepare_sprite_overlay(output))
    from prepare_integrated_stereo import prepare as prepare_stereo
    changed.extend(prepare_stereo(output))
    from prepare_integrated_loading import prepare as prepare_loading
    changed.extend(prepare_loading(output))
    from prepare_integrated_console_profile import prepare as prepare_console_profile
    changed.extend(prepare_console_profile(output))
    from prepare_integrated_toolchain import prepare as prepare_toolchain
    changed.extend(prepare_toolchain(output))
    changed.extend(rom_changes)
    manifest=dict(upstream_commit=lock['commit'],kind='native_pause_controls_display_and_render60_experiment',
        original_simulation_unchanged=False,simulation_changes='camera orbit, configurable local input and selectable native locomotion; original gameplay cadence retained',extra_snapshot_budget_bytes=2*1024*1024,
        rom_only=rom_manifest is not None,
        files={name:hashlib.sha256((output/name).read_bytes()).hexdigest() for name in changed})
    (output/'render60-overlay.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return output

if __name__=='__main__':
    root=ROOT/'third_party/sm64ds-3ds'
    print(prepare(root,ROOT/'build/integrated-native/render60-source',
        json.loads((ROOT/'reference-native.lock.json').read_text())))
