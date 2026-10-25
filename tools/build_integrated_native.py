#!/usr/bin/env python3
"""Build the credited, pinned complete native backend as a local candidate.

Preserves the existing runtime, upstream pin, ROM/pack, saves and historical
CIA. No emulator is launched and nothing is published. Public dependencies
may be downloaded; the private ROM is read locally by the reviewed extractor.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import time
import venv

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'third_party/sm64ds-3ds'
OUT=ROOT/'build/integrated-native'


def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def workspace_snapshot(root=ROOT):
    files=[root/'CMakeLists.txt',root/'reference-native.lock.json']
    for name in ('source/integrated_native','tools','tests','cmake'):
        files.extend(p for p in (root/name).rglob('*') if p.is_file()
                     and '__pycache__' not in p.parts and p.suffix in
                     {'.cpp','.c','.h','.hpp','.inc','.py','.cmake','.json'})
    return {p.relative_to(root).as_posix():digest(p) for p in sorted(files) if p.is_file()}


def require_unchanged_snapshot(before,root=ROOT):
    after=workspace_snapshot(root)
    changed=sorted(name for name in before.keys()|after.keys() if before.get(name)!=after.get(name))
    if changed:raise ValueError('Build inputs changed during compilation; refusing evidence freeze: '+', '.join(changed))


def verify_closed_candidate(candidate, elf, image):
    """Verify every frozen byte before reusing an immutable candidate."""
    candidate = Path(candidate).resolve()
    build = json.loads((candidate / 'BUILD.json').read_text())
    if build.get('status') != 'integrated_built_and_audited_emulator_pending':
        raise ValueError('Candidate is not closed: ' + str(candidate))
    files = build.get('files')
    if not isinstance(files, dict):
        raise ValueError('Missing candidate manifest')
    required = {'sm64ds-integrated.elf': digest(elf),
                'sm64ds-integrated.3dsx': digest(image)}
    if build.get('rom_only'):
        required['sd/3ds/sm64ds/sm64ds.3dsx'] = digest(image)
    for name, record in files.items():
        rel = PurePosixPath(name)
        path = candidate.joinpath(*rel.parts)
        if (rel.is_absolute() or '..' in rel.parts or rel.as_posix() != name
                or not path.is_file() or not path.resolve().is_relative_to(candidate)
                or any(p.is_symlink() for p in (path, *path.parents)
                       if p.is_relative_to(candidate))):
            raise ValueError('Invalid candidate file: ' + name)
        if (not isinstance(record, dict) or path.stat().st_size != record.get('bytes')
                or digest(path) != record.get('sha256')):
            raise ValueError('Closed candidate changed: ' + name)
    for name, expected in required.items():
        if files.get(name, {}).get('sha256') != expected:
            raise ValueError('Candidate executable mismatch: ' + name)
    if build.get('ELF_sha256') != digest(elf) or build.get('image_sha256') != digest(image):
        raise ValueError('Candidate receipt differs from the newly audited executables')
    return build


def run(command, cwd=ROOT, env=None, logfile=None):
    if logfile:
        with logfile.open('w') as stream:
            result=subprocess.run([str(x) for x in command],cwd=cwd,env=env,
                stdout=stream,stderr=subprocess.STDOUT)
        if result.returncode:
            print('\n'.join(logfile.read_text(errors='replace').splitlines()[-35:]))
            result.check_returncode()
    else:subprocess.run([str(x) for x in command],cwd=cwd,env=env,check=True)


def rom_reference(python,rom,decomp,explicit):
    """Make an independent private extraction oracle when no SD tree exists."""
    if explicit:
        reference=explicit.resolve();assert (reference/'sm64ds/repo').is_dir()
        return reference
    reference=SOURCE/'out/sd'
    if (reference/'sm64ds/repo').is_dir():return reference
    reference=OUT/'rom-reference-sd'
    if (reference/'sm64ds/repo').is_dir():return reference
    env={**os.environ,'SM64DS_DECOMP_DIR':str(decomp)}
    run([python,decomp/'tools/unpack.py',rom])
    run([python,SOURCE/'build3ds/extract_files.py',rom,decomp/'extracted/dsd/files'])
    run([python,decomp/'tools/asset_catalog.py','generate',rom],cwd=decomp)
    run([python,SOURCE/'port/tools/archive_catalog.py'],env=env)
    run([python,SOURCE/'port/tools/ntr_manifest.py'],env=env)
    repo=reference/'sm64ds/repo'
    shutil.copytree(decomp/'build/assets',repo/'build/assets')
    for name in ['files','archives']:
        shutil.copytree(decomp/'extracted/dsd'/name,repo/'extracted/dsd'/name)
    return reference


def main():
    if not __debug__:
        raise RuntimeError('Build verification requires Python assertions; do not use -O or PYTHONOPTIMIZE.')
    global SOURCE
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom',type=Path,default=ROOT/'build/roms/europe.nds')
    parser.add_argument('--python',type=Path)
    parser.add_argument('--jobs',type=int,default=4)
    parser.add_argument('--seed-decomp',type=Path,help='Optional local public checkout; cloned without hard links')
    parser.add_argument('--render60',action='store_true',help='Build the isolated render-only 60 Hz midpoint experiment')
    parser.add_argument('--rom-only',action='store_true',help='Find a compatible .nds ROM with any name in SD:/3ds/sm64ds/; requires --render60')
    parser.add_argument('--reference-sd',type=Path,help='Private extracted SD tree for the independent ROM installer gate')
    args=parser.parse_args();assert 1<=args.jobs<=16
    assert not args.rom_only or args.render60, '--rom-only requires the integrated render60 overlay'
    from build_version import reserve
    build_number = reserve(ROOT) if args.render60 else None
    if build_number is not None: print('Reserving BUILD '+str(build_number), flush=True)
    workspace_inputs=workspace_snapshot()
    lock=json.loads((ROOT/'reference-native.lock.json').read_text())
    for name,expected in lock['vendored_files_sha256'].items():
        assert digest(SOURCE/name)==expected, 'Reviewed vendor file changed: '+name
    rom=args.rom.resolve();assert rom.is_file()
    assert hashlib.sha1(rom.read_bytes()).hexdigest()==lock['ROM_SHA1'], 'Expected the verified EU revision'
    OUT.mkdir(parents=True,exist_ok=True)
    # Resolving a venv's interpreter symlink selects the system Python and
    # discards its isolated dependencies. Keep the invocation path intact.
    python=args.python.expanduser().absolute() if args.python else ROOT/'.venv/integrated-native/bin/python'
    if not python.exists():
        assert not args.python, 'Supplied Python does not exist'
        venv.EnvBuilder(with_pip=True).create(python.parent.parent)
    check=subprocess.run([str(python),'-c','import ndspy,capstone,elftools'],capture_output=True)
    if check.returncode:
        run([python,'-m','pip','install',*lock['python_packages']])
    if args.rom_only and subprocess.run([str(python),'-c','import PIL'],capture_output=True).returncode:
        run([python,'-m','pip','install','Pillow==12.3.0'])
    decomp=SOURCE/'sm64ds-decomp'
    if not decomp.exists():
        print('Preparing pinned public decomp dependency',flush=True)
        origin=args.seed_decomp.resolve() if args.seed_decomp else lock['decomp_repository']
        run(['git','clone','--no-checkout','--no-hardlinks',origin,decomp])
        run(['git','-C',decomp,'remote','set-url','origin',lock['decomp_repository']])
        found=subprocess.run(['git','-C',str(decomp),'cat-file','-e',lock['decomp_commit']],capture_output=True)
        if found.returncode:run(['git','-C',decomp,'fetch','--depth=1','origin',lock['decomp_commit']])
        run(['git','-C',decomp,'checkout','--detach',lock['decomp_commit']])
    actual=subprocess.check_output(['git','-C',decomp,'rev-parse','HEAD'],text=True).strip()
    assert actual==lock['decomp_commit'], 'Dependency revision changed; refusing reset'
    # Downloaded/migrated checkouts can acquire executable bits on every file.
    # Ignore that transport metadata while still rejecting every content edit.
    changes=subprocess.check_output(['git','-C',decomp,'-c','core.fileMode=false',
                                     'status','--porcelain','--untracked-files=no'],text=True)
    assert not changes.strip(), 'Dependency has tracked user edits; refusing overwrite'
    ppu_equivalence=None;affine_alpha_equivalence=None;native_ui_equivalence=None;raycast_equivalence=None;rom_install=None
    rom_manifest=None;rom_gate=None;setup_gate=None
    if args.rom_only:
        reference=rom_reference(python,rom,decomp,args.reference_sd)
        rom_manifest=OUT/'rom-bootstrap/rom_install_manifest.hpp'
        run([python,ROOT/'tools/generate_integrated_rom_manifest.py','--rom',rom,
             '--reference-sd',reference,'--decomp',decomp,'--output',rom_manifest,
             '--report',OUT/'rom-bootstrap/manifest-report.json'],logfile=OUT/'rom-manifest.log')
        rom_install=json.loads((OUT/'rom-bootstrap/manifest-report.json').read_text())
        run([python,ROOT/'tools/generate_integrated_rom_art.py','--rom',rom,'--decomp',decomp,
             '--output',OUT/'rom-bootstrap/rom_art_manifest.hpp',
             '--report',OUT/'rom-bootstrap/rom-art-report.json',
             '--preview-dir',OUT/'loading-authentic/rom-art-reference'],logfile=OUT/'rom-art.log')
        run([python,ROOT/'tools/generate_integrated_loading_style.py',
             '--output',OUT/'rom-bootstrap/loading_style.hpp'],logfile=OUT/'loading-style.log')
        from test_integrated_rom_bootstrap import check
        rom_gate=check(rom,reference,rom_manifest)
        from test_integrated_setup import check as check_setup
        setup_gate=check_setup(rom=rom)
    if args.render60:
        run([python,ROOT/'tests/test_build_tools.py'],logfile=OUT/'build-tools-test.log')
        run([python,ROOT/'tests/test_integrated_vm.py'],logfile=OUT/'diagnostics-reader-test.log')
        run([python,ROOT/'tests/test_read_mapped_capture.py'],logfile=OUT/'mapped-reader-test.log')
        run([python,ROOT/'tests/test_integrated_runtime_costs.py'],logfile=OUT/'runtime-costs-test.log')
        run([python,ROOT/'tests/options_camera/test_gameplay.py'],
            logfile=OUT/'gameplay-camera-host-test.log')
        run([python,ROOT/'tests/movement_lab/run_native_bridge.py'],
            logfile=OUT/'native-movement-host-test.log')
        run([python,ROOT/'tests/integrated_audio_save/run.py','--arm'],
            logfile=OUT/'audio-save-host-test.log')
        from test_integrated_vertex_inputs import check as check_vertex_inputs
        check_vertex_inputs()
        from test_integrated_mesh_uniforms import check as check_mesh_uniforms
        check_mesh_uniforms()
        run([python,ROOT/'tools/test_integrated_material_cache.py'],
            logfile=OUT/'material-cache-host-test.log')
        from test_integrated_raycast import check as check_raycast
        raycast_equivalence=check_raycast()
        from test_integrated_inactive_alpha import verify
        from test_integrated_affine_alpha import verify as verify_affine_alpha
        try:
            verify()
            verify_affine_alpha()
        except (OSError, AssertionError):
            # Fresh checkouts have no private capture receipts. Reproduce the
            # real-source differentials with explicitly synthetic inputs.
            from test_integrated_ppu_synthetic import check as check_synthetic_ppu
            check_synthetic_ppu()
        ppu_equivalence=verify()
        affine_alpha_equivalence=verify_affine_alpha()
        from test_integrated_native_ui import check as check_native_ui
        native_ui_equivalence=check_native_ui()
        from prepare_integrated_render60 import prepare
        SOURCE=prepare(SOURCE,OUT/'render60-source',lock,rom_manifest=rom_manifest)
        assert digest(SOURCE/'port/ntr/ppu.cpp')==native_ui_equivalence['derived_ppu_sha256']
        renderer=OUT/'test-integrated-render60'
        compiler=shutil.which('clang++') or shutil.which('c++');assert compiler
        run([compiler,'-std=c++17','-O1','-g','-fsanitize=address,undefined',
             '-fno-omit-frame-pointer','-I'+str(ROOT/'source/integrated_native'),
             '-I'+str(SOURCE/'port'),'-I'+str(SOURCE/'port/ntr/include'),
             ROOT/'tests/test_integrated_render60.cpp','-o',renderer],logfile=OUT/'render60-host-build.log')
        run([renderer],logfile=OUT/'render60-host-test.log')
        profile_probe=OUT/'test-profile-probe'
        run([compiler,'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',
             '-fsanitize=address,undefined','-fno-omit-frame-pointer',
             '-I'+str(ROOT/'source/integrated_native/options'),'-I'+str(SOURCE/'port/platform/3ds'),
             ROOT/'tests/test_profile_probe.cpp','-o',profile_probe],logfile=OUT/'profile-probe-host-build.log')
        run([profile_probe],logfile=OUT/'profile-probe-host-test.log')
    preserved=[ROOT/'upstream.lock',ROOT/'build/europe.pak',rom]
    preserved += sorted((ROOT/'dist').glob('*.cia')) if (ROOT/'dist').exists() else []
    before={str(p.relative_to(ROOT)) if p.is_relative_to(ROOT) else str(p):digest(p)
            for p in preserved if p.exists()}
    env={**os.environ,'SM64DS_ROM':str(rom),'PYTHON':str(python),'JOBS':str(args.jobs)}
    compiler=shutil.which('clang++') or shutil.which('c++')
    assert compiler, 'A host C++ compiler is required for the controls gate'
    controls=OUT/'test-integrated-controls'
    run([compiler,'-std=c++17','-O1','-g','-fsanitize=address,undefined',
         '-fno-omit-frame-pointer','-I'+str(SOURCE/'port'),
         *(['-DSM64DS_ROM_ONLY_DEFAULTS=1'] if args.rom_only else []),
         ROOT/'tests/test_integrated_controls.cpp',
         SOURCE/'port/platform/3ds/enhance_settings.cpp','-o',controls],logfile=OUT/'controls-build.log')
    run([controls],logfile=OUT/'controls-test.log')
    if args.render60:
        for test, files in {
            'camera':['tests/options_camera/test.cpp','port/platform/3ds/cstick_camera.cpp','port/platform/3ds/analog_pad.cpp','port/platform/3ds/enhance_settings.cpp'],
            'stylus-camera':['tests/options_camera/test_stylus.cpp'],
            'intro-skip':['tests/options_menu/test_intro_skip.cpp'],
            'menu':['tests/options_menu/test_menu.cpp','port/platform/3ds/enhance_overlay.cpp','port/platform/3ds/enhance_settings.cpp'],
            'motion-buttons':['tests/movement_lab/test_motion_buttons.cpp'],
            'motion-terrain-retry':['tests/movement_lab/test_retry_gate.cpp'],
            'motion-effect-ceiling':['tests/movement_lab/test_distant_ceiling.cpp'],
            'native-pause':['tests/options_menu/test_native_pause.cpp','port/platform/3ds/native_pause_menu.cpp','port/platform/3ds/enhance_settings.cpp'],
            'diagnostic-panel':['tests/options_menu/test_diagnostic_panel.cpp'],
            'render-diagnostic':['tests/options_menu/test_render_diagnostic.cpp'],
            'dump-transfer':['tests/options_menu/test_dump_transfer.cpp'],
            'dump-folder':['tests/options_menu/test_dump_folder.cpp'],
            'sprite-layout':['tests/options_menu/test_sprite_layout.cpp'],
            'ui-layout':['tests/options_menu/test_ui_layout.cpp'],
            'level-files':['tests/options_menu/test_level_files.cpp'],
            'console-profile':['tests/options_menu/test_console_profile.cpp'],
            'overlay-publication':['tests/test_overlay_publication.cpp'],
            'profile-probe':['tests/test_profile_probe.cpp'],
            'native-lower-art':['tests/options_menu/test_native_lower_art.cpp'],
            'native-pause-art':['tests/options_menu/test_native_pause_art.cpp','port/platform/3ds/native_pause_menu.cpp','port/platform/3ds/enhance_settings.cpp'],
            'benchmark':['tests/native_benchmark/test_model.cpp'],
            'display':['tests/options_display/test_plan.cpp'],
            'title-background':['tests/options_display/test_title_background.cpp'],
        }.items():
            sources=[ROOT/n if n.startswith('tests/') else SOURCE/n for n in files]
            target=OUT/('options-'+test+'-test')
            run([compiler,'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                 '-I'+str(SOURCE/'port/platform/3ds'),'-I'+str(SOURCE/'port/platform/3ds/movement'),'-I'+str(SOURCE/'port/platform/3ds/movement/core/src'),'-I'+str(SOURCE/'port/ntr/include'),'-I'+str(SOURCE/'port'),'-I'+str(ROOT/'source/integrated_native'),
                 '-I'+str(ROOT/'source/integrated_native/options'),*sources,'-o',target],logfile=OUT/('options-'+test+'-build.log'))
            run([target],cwd=OUT,logfile=OUT/('options-'+test+'-test.log'))
        culling=OUT/'options-culling-test'
        run([compiler,'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
             '-I'+str(ROOT/'tests/options_display/clipper_shim'),'-I'+str(ROOT/'source/integrated_native/options'),
             ROOT/'tests/options_display/test_culling.cpp',SOURCE/'port/platform/3ds/display_culling.cpp',
             SOURCE/'sm64ds-decomp/src/_ZN7Clipper13Func_0201559CEv.cpp','-o',culling],logfile=OUT/'options-culling-build.log')
        run([culling],logfile=OUT/'options-culling-test.log')
        run([python,ROOT/'tests/options_menu/test_native_pause_hooks.py'],logfile=OUT/'options-native-pause-hooks-test.log')
        intro_fixture=OUT/'benchmark-intro-fixture-test'
        run([compiler,'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
             '-I'+str(SOURCE/'sm64ds-decomp/include'),ROOT/'tests/native_benchmark/test_intro_fixture.cpp',
             SOURCE/'sm64ds-decomp/src/_ZN8SaveData17SetCharacterIntroEi.cpp','-o',intro_fixture],
             logfile=OUT/'benchmark-intro-fixture-build.log')
        run([intro_fixture],logfile=OUT/'benchmark-intro-fixture-test.log')
    controls_report=dict(status='passed',ASan=True,UBSan=True,
        scope='Actual settings/C-Stick sources with controlled host samples; no hardware/HID acceptance',
        sources={str(p.relative_to(ROOT)):digest(p) for p in [ROOT/'tests/test_integrated_controls.cpp',
            SOURCE/'port/platform/3ds/cstick_camera.cpp',SOURCE/'port/platform/3ds/enhance_settings.cpp',
            SOURCE/'port/platform/3ds/enhance_settings.h']},
        output=(OUT/'controls-test.log').read_text())
    (OUT/'controls-gate.json').write_text(json.dumps(controls_report,indent=2)+'\n')
    print('Building all integrated game/runtime/audio/render/control providers; emulator remains closed',flush=True)
    run(['bash',SOURCE/'build.sh','--native'],cwd=SOURCE,env=env,logfile=OUT/'build.log')
    elf=SOURCE/'build3ds/user-build/sm64ds.elf';image=SOURCE/'build3ds/user-build/sm64ds.3dsx'
    print('Auditing packed scripts, native providers and ELF/3DSX bytes',flush=True)
    run([python,ROOT/'tools/audit_integrated_native.py','--source',SOURCE,'--elf',elf,
         '--image',image,'--report',OUT/'pre-emulator-audit.json'],logfile=OUT/'audit.log')
    audit=json.loads((OUT/'pre-emulator-audit.json').read_text())
    assert audit['status']=='passed_before_emulator'
    require_unchanged_snapshot(workspace_inputs)
    candidate=OUT/('candidate-'+digest(elf)[:16])
    if (candidate/'BUILD.json').is_file():
        existing=verify_closed_candidate(candidate,elf,image)
        # Identical executable bytes can come from a newer preparation/test
        # pipeline. Preserve the old evidence and freeze the current inputs in
        # a separate candidate instead of silently returning stale provenance.
        if existing.get('workspace_inputs')!=workspace_inputs:
            identity=hashlib.sha256(json.dumps(workspace_inputs,sort_keys=True).encode()).hexdigest()[:12]
            candidate=OUT/(candidate.name+'-inputs-'+identity)
    if candidate.exists() and not (candidate/'BUILD.json').is_file():
        # A prior interrupted evidence freeze is diagnostic output, not an
        # immutable candidate. Preserve it and start a fresh complete freeze.
        candidate.rename(OUT/('incomplete-'+candidate.name+'-'+str(time.time_ns())))
    if candidate.exists():
        existing=verify_closed_candidate(candidate,elf,image)
        if existing.get('workspace_inputs')!=workspace_inputs:raise ValueError('Candidate workspace inputs differ')
        if existing.get('rom_only')!=args.rom_only or existing.get('render60_experimental')!=args.render60:
            raise ValueError('Candidate build configuration differs')
        if args.render60:
            from gate_integrated_render60 import gate
            gate(candidate)
        (OUT/'latest.json').write_text(json.dumps(existing,indent=2)+'\n')
        print('Exact closed candidate already exists: '+str(candidate));return
    candidate.mkdir()
    shutil.copyfile(elf,candidate/'sm64ds-integrated.elf')
    shutil.copyfile(image,candidate/'sm64ds-integrated.3dsx')
    shutil.copyfile(OUT/'pre-emulator-audit.json',candidate/'pre-emulator-audit.json')
    shutil.copyfile(SOURCE/'LICENSE',candidate/'LICENSE')
    if args.rom_only:
        # The deliverable requires only the user's ROM. No pre-extracted tree,
        # ROM, DSP firmware, settings, saves or diagnostics are included.
        target=candidate/'sd/3ds/sm64ds/sm64ds.3dsx';target.parent.mkdir(parents=True)
        shutil.copyfile(image,target)
    else:
        shutil.copytree(SOURCE/'out/sd',candidate/'sd')
        # New candidate-only settings; no existing SD/save/settings are modified.
        (candidate/'sd/sm64ds/settings.ini').write_text('[input]\ncircle_pad = analog\ncamera_cstick = free\n')
    if args.render60:
        # Preserve the actual adapted bytes, not just the mutable work copy or
        # its hashes. The vendor/decomp pins plus this overlay reproduce it.
        evidence=candidate/'source-evidence';evidence.mkdir()
        overlay=json.loads((SOURCE/'render60-overlay.json').read_text())
        for name,expected in overlay['files'].items():
            assert digest(SOURCE/name)==expected
            target=evidence/'overlay'/name;target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(SOURCE/name,target)
        for name in ['reference-native.lock.json','tools/build_integrated_native.py',
                     'tools/prepare_integrated_profiling.py','tools/prepare_integrated_runtime_costs.py','tests/test_profile_probe.cpp','tests/test_integrated_runtime_costs.py',
                     'tools/devkitpro_tools.py','tools/test_integrated_ppu_synthetic.py',
                     'tools/prepare_integrated_toolchain.py',
                     'tools/prepare_integrated_audio.py','tools/prepare_integrated_save.py',
                     'tests/test_overlay_publication.cpp','tests/test_profile_probe.cpp','tests/test_build_tools.py','tools/movement_lab/port_source.py',
                     'CMakeLists.txt','cmake/IntegratedHostTests.cmake',
                     'tools/prepare_integrated_vertex_inputs.py','tools/test_integrated_vertex_inputs.py',
                     'tools/prepare_integrated_mesh_uniforms.py','tools/test_integrated_mesh_uniforms.py','source/integrated_native/mesh_uniform_cache.hpp',
                     'tools/prepare_integrated_render60.py','tools/prepare_integrated_movement.py','tools/movement_lab/build_core.py','tools/prepare_integrated_camera.py',
                     'tools/prepare_integrated_collision.py','tools/test_integrated_raycast.py','source/integrated_native/movement/raycast_arithmetic.hpp','tests/movement_lab/test_raycast_arithmetic.cpp',
                     'tools/prepare_integrated_benchmark.py','tools/prepare_integrated_pause_scene.py','tools/prepare_integrated_native_ui.py','tools/test_integrated_native_ui.py','tools/prepare_integrated_draw_material.py','tools/test_integrated_material_cache.py','source/integrated_native/draw_material_cache.hpp','tools/generate_native_volume_slider.py','tools/generate_diagnostics_skin.py','tools/generate_native_lower_art.py',
                     'tools/prepare_integrated_display.py','tools/prepare_integrated_title_background.py','tools/prepare_integrated_native_pause.py','tools/prepare_integrated_options_hooks.py',
                     'tools/generate_integrated_options_art.py','tools/gate_integrated_render60.py',
                     'tools/replay_camera_oam.py','tools/decode_lower_obj_fixture.cpp',
                     'tools/run_integrated_native.py','tools/audit_integrated_native.py',
                     'tools/inspect_integrated_vm.py','tools/read_mapped_capture.py','tests/test_read_mapped_capture.py','tools/test_integrated_inactive_alpha.py',
                     'tools/test_integrated_affine_alpha.py','tests/integrated_affine_alpha_replay.cpp',
                     'source/integrated_native/affine_alpha_objects.inc','source/integrated_native/affine_overlay_key.hpp',
                     'tests/integrated_ppu_replay.cpp','tests/test_integrated_render60.cpp',
                     'tests/test_integrated_controls.cpp','tests/test_integrated_vm.py']:
            target=evidence/'workspace'/name;target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(ROOT/name,target)
        for directory in ['tests/native_benchmark','tests/options_camera','tests/options_menu','tests/options_display','tests/integrated_audio_save','tests/movement_lab','source/integrated_native/options','source/integrated_native/movement']:
            for source in sorted((ROOT/directory).rglob('*')):
                if not source.is_file() or source.suffix not in {'.cpp','.c','.h','.hpp','.py','.md','.inc'}:continue
                target=evidence/'workspace'/source.relative_to(ROOT)
                target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
        if args.rom_only:
            for name in ['tools/generate_integrated_rom_manifest.py','tools/test_integrated_rom_bootstrap.py','tools/test_integrated_setup.py',
                         'tools/generate_integrated_rom_art.py','tools/generate_integrated_loading_style.py','tests/test_integrated_rom_art.cpp',
                         'tests/test_integrated_rom_bootstrap.cpp','tests/test_integrated_loading.cpp',
                         'tests/test_integrated_dsp_bootstrap.cpp',
                         'source/integrated_native/boot_allocator.hpp','source/integrated_native/rom_bootstrap.hpp','source/integrated_native/rom_discovery.hpp','tests/test_integrated_rom_discovery.cpp','source/integrated_native/rom_setup_3ds.hpp',
                         'source/integrated_native/loading_art.hpp','source/integrated_native/loading_locale.hpp','source/integrated_native/rom_art.hpp',
                         'source/integrated_native/packed_assets.hpp','source/integrated_native/packed_assets_3ds.hpp','tests/test_integrated_packed_assets.cpp',
                         'source/integrated_native/packed_builder.hpp','tests/test_integrated_packed_builder.cpp',
                         'source/integrated_native/setup_power.hpp','tests/test_integrated_setup_power.cpp',
                         'source/integrated_native/dsp_bootstrap.hpp','source/integrated_native/loading_screen.hpp',
                         'source/integrated_native/loading_screen_3ds.hpp']:
                target=evidence/'workspace'/name;target.parent.mkdir(parents=True,exist_ok=True)
                shutil.copyfile(ROOT/name,target)
            (evidence/'rom-install.json').write_text(json.dumps(rom_install,indent=2)+'\n')
            (evidence/'rom-bootstrap-gate.json').write_text(json.dumps(rom_gate,indent=2)+'\n')
            (evidence/'setup-host-gate.json').write_text(json.dumps(setup_gate,indent=2)+'\n')
            shutil.copyfile(OUT/'rom-bootstrap/rom-art-report.json',evidence/'rom-art-report.json')
        (evidence/'ppu-equivalence.json').write_text(json.dumps(ppu_equivalence,indent=2)+'\n')
        (evidence/'affine-alpha-equivalence.json').write_text(json.dumps(affine_alpha_equivalence,indent=2)+'\n')
        (evidence/'native-ui-ppu-equivalence.json').write_text(json.dumps(native_ui_equivalence,indent=2)+'\n')
        (evidence/'raycast-arithmetic-gate.json').write_text(json.dumps(raycast_equivalence,indent=2)+'\n')
        if 'port/platform/3ds/draw_material_cache.hpp' in overlay['files']:
            shutil.copyfile(OUT/'material-cache-gate-latest.json',evidence/'material-register-equivalence.json')
        (evidence/'manifest.json').write_text(json.dumps(dict(
            upstream_commit=lock['commit'],decomp_commit=actual,
            files={str(p.relative_to(evidence)):digest(p)
                   for p in sorted(evidence.rglob('*')) if p.is_file()}),indent=2)+'\n')
    for p in preserved:
        if p.exists():
            key=str(p.relative_to(ROOT)) if p.is_relative_to(ROOT) else str(p)
            assert digest(p)==before[key], 'Protected existing input/artifact changed'
    files={str(p.relative_to(candidate)):{'bytes':p.stat().st_size,'sha256':digest(p)}
           for p in sorted(candidate.rglob('*')) if p.is_file()}
    report=dict(status='integrated_built_and_audited_emulator_pending',
        build_number=build_number,
        repository=lock['repository'],commit=lock['commit'],decomp_commit=actual,
        candidate=str(candidate),ELF_sha256=digest(elf),image_sha256=digest(image),
        protected_files=before,files=files,default_camera='free',circle_pad='analog',
        features=['native GPU geometry/composition/shadows','original SDAT audio and DSP output',
                  'original actor/level/overlay lifecycle','packed Kuppa scripts',
                  'Circle Pad analog movement','C-Stick free yaw/pitch','save persistence implementation',
                  'optional stereo and settings','real-time cadence with presentation skipping'],
        physical_hardware=False,gameplay_verified=False,Old3DS_60FPS_verified=False,
        workspace_inputs=workspace_inputs,
        rom_only=args.rom_only,
        diagnostics_default=True if args.rom_only else None,
        dsp_source='existing_component_or_console_home_menu' if args.rom_only else None,
        rom_install=dict(schema_id=rom_install['schema_id'],files=rom_install['file_count'],
                         bytes=rom_install['total_bytes'],ROM_SHA1=lock['ROM_SHA1'],
                         directory='sdmc:/3ds/sm64ds',extension='.nds',filename='any',
                         multiple_candidates='lexical_header_then_full_SHA1_CRC32',
                         preparation='single_SM64DSP1_container',output='sdmc:/3ds/sm64ds/prepared.sm64ds') if args.rom_only else None,
        render60_experimental=args.render60,
        render60_overlay=json.loads((SOURCE/'render60-overlay.json').read_text()) if args.render60 else None,
        attribution='Sébastien Breton and contributors; MIT; original decomp Tango and contributors')
    (candidate/'BUILD.json').write_text(json.dumps(report,indent=2)+'\n')
    if args.render60:
        from gate_integrated_render60 import gate
        gate(candidate)
    (OUT/'latest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['status','candidate','ELF_sha256','image_sha256']},indent=2))


if __name__=='__main__':main()
