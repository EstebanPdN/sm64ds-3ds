#!/usr/bin/env python3
"""Require the exact interpolation sources, owned-state tests and linked hooks."""
import hashlib
import json
from pathlib import Path
from devkitpro_tools import arm_tool, sdk_tool
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/integrated-native'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def gate(candidate):
    build=json.loads((candidate/'BUILD.json').read_text());assert build['render60_experimental']
    overlay=build['render60_overlay'];source=OUT/'render60-source'
    for name,expected in overlay['files'].items():assert sha(source/name)==expected,name
    assert sha(ROOT/'source/integrated_native/render_60.hpp')==overlay['files']['port/platform/3ds/render_60.hpp']
    from test_integrated_inactive_alpha import verify
    ppu=verify()
    from test_integrated_affine_alpha import verify as verify_affine_alpha
    affine=verify_affine_alpha()
    from test_integrated_native_ui import verify as verify_native_ui
    native_ui=verify_native_ui()
    assert overlay['files']['port/ntr/ppu.cpp']==native_ui['derived_ppu_sha256']
    evidence=candidate/'source-evidence'
    assert json.loads((evidence/'ppu-equivalence.json').read_text())==ppu
    assert json.loads((evidence/'affine-alpha-equivalence.json').read_text())==affine
    assert json.loads((evidence/'native-ui-ppu-equivalence.json').read_text())==native_ui
    for name,expected in json.loads((evidence/'manifest.json').read_text())['files'].items():
        assert sha(evidence/name)==expected,name
    for name,expected in overlay['files'].items():assert sha(evidence/'overlay'/name)==expected,name
    compiler=shutil.which('clang++') or shutil.which('c++');assert compiler
    test=OUT/'test-integrated-render60'
    command=[compiler,'-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
        '-I'+str(ROOT/'source/integrated_native'),'-I'+str(source/'port'),
        '-I'+str(source/'port/ntr/include'),str(ROOT/'tests/test_integrated_render60.cpp'),'-o',str(test)]
    subprocess.run(command,check=True,capture_output=True,text=True)
    result=subprocess.run([str(test)],check=True,capture_output=True,text=True)
    subprocess.run([sys.executable,str(ROOT/'tests/test_integrated_vm.py')],check=True,capture_output=True,text=True)
    symbols=subprocess.check_output([arm_tool('nm'),
        str(candidate/'sm64ds-integrated.elf')],text=True)
    required=['__wrap_func_02043fdc','sm64ds_render60_cut','sm64ds_render60_actor_order',
        'sm64ds_render60_current_actor','sm64ds_render60_need_exact',
        'sm64ds_render60_source_epoch','sm64ds_render60_scene','_ZN8render6013interpolationE']
    required.append('_ZN3ntr32ppu_prepare_affine_alpha_objectsENS_6EngineE')
    if 'port/platform/3ds/options_art.hpp' in overlay['files']:
        required.extend(['sm64ds_native_pause_take_requests','sm64ds_native_pause_begin','sm64ds_cstick_cam_follow','sm64ds_cstick_cam_height','sm64ds_cstick_cam_orbit','sm64ds_cstick_cam_direct'])
    for name in required:assert any(line.endswith(' '+name) for line in symbols.splitlines()),name
    files=['source/integrated_native/render_60.hpp','source/integrated_native/native_diagnostics.hpp','tests/test_integrated_render60.cpp',
        'tools/prepare_integrated_render60.py','tools/gate_integrated_render60.py',
        'tools/inspect_integrated_vm.py','tests/test_integrated_vm.py',
        'tools/test_integrated_inactive_alpha.py','tests/integrated_ppu_replay.cpp',
        'tools/test_integrated_affine_alpha.py','tests/integrated_affine_alpha_replay.cpp',
        'source/integrated_native/affine_alpha_objects.inc','source/integrated_native/affine_overlay_key.hpp',
        'tools/build_integrated_native.py','tools/run_integrated_native.py']
    files.extend(native_ui['sources'])
    material=None
    if 'port/platform/3ds/draw_material_cache.hpp' in overlay['files']:
        material=json.loads((OUT/'material-cache-gate-latest.json').read_text())
        assert material['status']=='actual_original_material_register_trace_equivalent' and material['ASan'] and material['UBSan']
        for name,expected in material['sources'].items():assert sha(ROOT/name)==expected,name
        assert json.loads((evidence/'material-register-equivalence.json').read_text())==material
        assert overlay['files']['port/platform/3ds/draw_material_cache.hpp']==sha(ROOT/'source/integrated_native/draw_material_cache.hpp')
        files.extend(material['sources'])
    setup=None;rom=None
    if build.get('rom_only'):
        from test_integrated_setup import check as check_setup
        setup=check_setup(verify_only=True)
        for name,expected in setup['sources'].items():assert sha(ROOT/name)==expected,name
        rom=json.loads((OUT/'rom-bootstrap/host-gate.json').read_text())
        assert rom['status']=='passed' and rom['ASan'] and rom['UBSan']
        for name,expected in rom['sources'].items():assert sha(Path(name))==expected,name
        assert json.loads((evidence/'setup-host-gate.json').read_text())==setup
        assert json.loads((evidence/'rom-bootstrap-gate.json').read_text())==rom
        assert not (candidate/'sd/sm64ds/repo').exists()
        assert list(p.relative_to(candidate/'sd').as_posix() for p in (candidate/'sd').rglob('*') if p.is_file())==['3ds/sm64ds/sm64ds.3dsx']
        files.extend(name for name in setup['sources'] if name not in files)
        files.extend(['tools/test_integrated_rom_bootstrap.py','tools/generate_integrated_rom_manifest.py',
                      'source/integrated_native/rom_bootstrap.hpp','tests/test_integrated_rom_bootstrap.cpp'])
    report=dict(status='passed_before_emulator',ELF_sha256=sha(candidate/'sm64ds-integrated.elf'),
        ASan=True,UBSan=True,linked_hooks=required,output=result.stdout,
        sources={name:sha(ROOT/name) for name in files},overlay=overlay,
        ppu_pixel_equivalence=ppu,
        affine_alpha_pixel_equivalence=affine,
        material_register_equivalence=material,
        rom_only_setup_host=setup,rom_installer_host=rom,
        simulation_cadence_changed=False,
        gameplay_adapters=['camera orbit','analog input','selectable native locomotion'],
        physical_60FPS=False)
    path=OUT/('render60-readiness-'+build['ELF_sha256'][:16]+'.json')
    path.write_text(json.dumps(report,indent=2)+'\n');print(path)
    return report
if __name__=='__main__':
    candidate=Path(sys.argv[1]) if len(sys.argv)>1 else Path(json.loads((OUT/'latest.json').read_text())['candidate'])
    gate(candidate)
