#!/usr/bin/env python3
"""Host-only causal audio/storage regressions against generated production code.

No emulator, ROM or personal save is used. All writes use a new build directory
or TemporaryDirectory. Can be called independently by the integrated builder.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
TESTS = Path(__file__).resolve().parent
VENDOR = ROOT / 'third_party/sm64ds-3ds'
sys.path.insert(0, str(ROOT / 'tools'))
import prepare_integrated_audio as audio
import prepare_integrated_save as save


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    out = ROOT / 'build/integrated-native' / ('audio-save-host-' + str(time.time_ns()))
    out.mkdir(parents=True)
    lock = json.loads((ROOT / 'reference-native.lock.json').read_text())
    tracked = ['port/hal/save_cart.cpp', 'port/tests/smoke_save_cart.cpp',
               'port/tests/menu_save_transactions.cpp', 'port/tools/menu_sources.py',
               *[str(p.relative_to(VENDOR)) for p in (VENDOR / 'port/audio').iterdir() if p.is_file()]]
    before = {name: sha(VENDOR / name) for name in tracked}
    for name, digest in before.items():
        assert digest == lock['vendored_files_sha256'][name], name
    generated = out / 'generated'
    shutil.copytree(VENDOR / 'port/audio', generated / 'port/audio')
    (generated / 'port/hal').mkdir()
    shutil.copyfile(VENDOR / 'port/hal/save_cart.cpp', generated / 'port/hal/save_cart.cpp')
    changes = audio.prepare(generated) + save.prepare(generated)
    options = ROOT / 'source/integrated_native/options'
    for name in ['enhance_settings.cpp', 'enhance_settings.h']:
        shutil.copyfile(options / name, generated / 'port/platform/3ds' / name)

    # Repeated application/drifting anchors must fail before changing files.
    for patcher, target in [(audio, 'port/audio/sound_output_3ds.cpp'),
                            (save, 'port/hal/save_cart.cpp')]:
        path = generated / target
        unchanged = path.read_bytes()
        try:
            patcher.prepare(generated)
            raise AssertionError('repeated patch accepted')
        except ValueError:
            pass
        assert path.read_bytes() == unchanged
        try:
            patcher.prepare(VENDOR)
            raise AssertionError('vendor mutation accepted')
        except ValueError:
            pass
    raw = (VENDOR / 'port/audio/sound_output_3ds.cpp').read_text()
    legacy = audio.LEGACY_INCLUDE + raw.replace('        DSP_FlushDataCache(',
                                                audio.LEGACY_GAIN + '        DSP_FlushDataCache(', 1)
    assert audio.patch_output(raw) == audio.patch_output(legacy)
    for patcher, text in [(audio.patch_output, raw.replace('constexpr unsigned Chunk=128;', 'constexpr unsigned Chunk=129;')),
                          (save.patch_cart, (VENDOR / 'port/hal/save_cart.cpp').read_text().replace(
                              'if (std::fflush(file)!=0) good=false;', 'if (std::fflush(file)!=0) return false;'))]:
        try:
            patcher(text)
            raise AssertionError('changed contract accepted')
        except ValueError:
            pass
    compiler = shutil.which('clang++')
    c_compiler = shutil.which('clang')
    assert compiler and c_compiler
    common = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
              '-fno-omit-frame-pointer', '-Wall', '-Wextra', '-Werror']
    reports = {}

    def run(command, label, **kwargs):
        result = subprocess.run([str(x) for x in command], capture_output=True, text=True, **kwargs)
        (out / (label + '.log')).write_text(result.stdout + result.stderr)
        if result.returncode:
            print(result.stdout + result.stderr, file=sys.stderr)
        result.check_returncode()
        return result

    base = generated / 'port/platform/3ds'
    for name, sources in {
        'audio': [TESTS / 'test_audio.cpp'],
        'storage': [TESTS / 'test_storage.cpp'],
        'settings-io': [TESTS / 'test_settings_io.cpp', base / 'enhance_settings.cpp'],
        'boot-settings': [TESTS / 'test_boot_settings.cpp', base / 'enhance_settings.cpp'],
        'ndsp-output': [TESTS / 'test_output.cpp', generated / 'port/audio/sound_output_3ds.cpp',
                        base / 'enhance_settings.cpp'],
    }.items():
        run([compiler, '-std=c++17', *common, '-I' + str(base), '-I' + str(TESTS),
             '-I' + str(generated / 'port/audio'), *sources, '-o', out / name], name + '-build')
        reports[name] = run([out / name], name + '-run').stdout
    run([compiler, '-std=c++17', '-D__3DS__', *common, '-I' + str(base),
         TESTS / 'test_storage.cpp', '-o', out / 'storage-3ds-policy'], 'storage-3ds-policy-build')
    reports['storage-3ds-policy'] = run([out / 'storage-3ds-policy'], 'storage-3ds-policy-run').stdout
    benchmark = None
    if '--bench' in sys.argv[1:]:
        run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
             '-I' + str(base), TESTS / 'benchmark_gain.cpp', '-o', out / 'gain-bench'], 'gain-bench-build')
        benchmark = run([out / 'gain-bench'], 'gain-bench-run').stdout

    # Preserve the original SaveData format and menu return values, using
    # the same pinned smoke probes against the *patched* generated HAL.
    repo = VENDOR / 'sm64ds-decomp'
    includes = ['-I' + str(repo / 'include')]
    run([compiler, '-std=c++17', '-DSM64DS_PLATFORM_PC', *common, *includes,
         VENDOR / 'port/tests/smoke_save_cart.cpp', generated / 'port/hal/save_cart.cpp',
         '-o', out / 'cart'], 'cart-build')
    with tempfile.TemporaryDirectory(prefix='sm64ds-new-cart-') as temporary:
        (Path(temporary) / 'sdmc:/sm64ds').mkdir(parents=True)
        reports['cart'] = run([out / 'cart'], 'cart-run', cwd=temporary).stdout
    spec = importlib.util.spec_from_file_location('pinned_menu_sources', VENDOR / 'port/tools/menu_sources.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    dispatcher = out / 'dispatcher.c'
    module.generate(repo / 'src/func_ov007_020cc600.c', dispatcher)
    objects = []
    # Pinned decomp source is already covered by its fixed git revision.
    assert subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip() == lock['decomp_commit']
    for i, source in enumerate([dispatcher, repo / 'src/func_02013c84.c']):
        obj = out / (str(i) + '.o')
        objects.append(obj)
        run([c_compiler, '-c', *common, *includes, source, '-o', obj], 'menu-c-' + str(i))
    sources = ['_ZN8SaveData16SetDefaultValuesEP12FileSaveData.cpp',
               '_ZN8SaveData12ReadFileDataEjP12FileSaveData.cpp',
               '_ZN8SaveData8SaveFileEjP12FileSaveData.cpp',
               '_ZN8SaveData13EraseSaveFileEjPc.cpp']
    run([compiler, '-std=c++17', '-DSM64DS_PLATFORM_PC', *common, *includes,
         VENDOR / 'port/tests/menu_save_transactions.cpp', generated / 'port/hal/save_cart.cpp',
         *[repo / 'src' / name for name in sources], *objects, '-o', out / 'menu'], 'menu-build')
    with tempfile.TemporaryDirectory(prefix='sm64ds-new-menu-save-') as temporary:
        reports['menu'] = run([out / 'menu', Path(temporary) / 'save.sav'], 'menu-run').stdout
    arm_compile = {}
    sdk = None
    if '--arm' in sys.argv[1:]:
        devkitpro = Path(os.environ['DEVKITPRO'])
        devkitarm = Path(os.environ['DEVKITARM'])
        arm_cxx = devkitarm / 'bin/arm-none-eabi-g++'
        arm_flags = ['-std=gnu++17', '-O2', '-march=armv6k', '-mtune=mpcore',
                     '-mfpu=vfp', '-mfloat-abi=hard', '-mtp=soft', '-mword-relocations',
                     '-ffunction-sections', '-fno-strict-aliasing', '-fsigned-char',
                     '-D__3DS__', '-DSM64DS_PLATFORM_PC', '-Wall', '-Wextra', '-Werror',
                     '-I' + str(repo / 'include'), '-isystem', str(devkitpro / 'libctru/include')]
        for source in [base / 'enhance_settings.cpp', generated / 'port/hal/save_cart.cpp',
                       generated / 'port/audio/sound_output_3ds.cpp']:
            obj = out / (source.stem + '.arm.o')
            run([arm_cxx, *arm_flags, '-c', source, '-o', obj], source.stem + '-arm-build')
            arm_compile[str(source.relative_to(generated))] = sha(obj)
        snapshot = out / 'snapshot_link.arm.o'
        run([arm_cxx, *arm_flags, '-I' + str(base), '-c', TESTS / 'snapshot_link.cpp', '-o', snapshot],
            'snapshot-arm-build')
        combined = out / 'audio-snapshot-relocatable.arm.o'
        run([arm_cxx, '-r', out / 'sound_output_3ds.arm.o', snapshot, '-o', combined], 'snapshot-arm-link')
        nm = run([devkitarm / 'bin/arm-none-eabi-nm', '-C', combined], 'snapshot-arm-nm').stdout
        counter = [line for line in nm.splitlines() if ' enhance::audio_diagnostics::counters' in line]
        assert len(counter) == 1 and ' U ' not in counter[0], counter
        arm_compile['snapshot_link.arm.o'] = sha(snapshot)
        arm_compile['audio-snapshot-relocatable.arm.o'] = sha(combined)
        library = devkitpro / 'libctru/lib/libctru.a'
        sdk_object = out / 'sdk_archive_dev.o'
        extracted = subprocess.run([devkitarm / 'bin/arm-none-eabi-ar', 'p', library, 'archive_dev.o'],
                                   capture_output=True, check=True)
        sdk_object.write_bytes(extracted.stdout)
        disassembly = run([devkitarm / 'bin/arm-none-eabi-objdump', '-dr', '--disassemble=archive_rename', sdk_object],
                          'sdk-archive-rename').stdout
        assert disassembly.count('R_ARM_CALL\tFSUSER_RenameFile') == 2
        assert disassembly.count('R_ARM_CALL\tFSUSER_DeleteFile') == 1
        package = devkitpro.parents[1] / 'var/lib/pacman/local'
        versions = sorted(package.glob('libctru-*/desc'))
        sdk = dict(libctru_sha256=sha(library), archive_dev_sha256=sha(sdk_object),
                   packaged_version=versions[0].parent.name if versions else 'unknown',
                   rename_references=2, delete_references=1, snapshot_link_counter_symbol=counter[0],
                   limits='Installed ARM archive disassembly; not SDK source reproduction or physical filesystem validation.')
    assert all(sha(VENDOR / name) == digest for name, digest in before.items())
    own = [Path(__file__), ROOT / 'tools/prepare_integrated_audio.py', ROOT / 'tools/prepare_integrated_save.py',
           *TESTS.glob('*.cpp'), TESTS / '3ds.h',
           *[options / name for name in ['audio_gain.hpp', 'audio_diagnostics.hpp', 'atomic_file.hpp',
                                        'enhance_settings.cpp', 'enhance_settings.h']]]
    report = dict(status='passed', ASan=True, UBSan=True, tests=reports,
                  changed_generated_files=changes, vendor_unchanged=True,
                  sources={str(p.relative_to(ROOT)): sha(p) for p in own},
                  vendor_sources=before, decomp_commit=lock['decomp_commit'],
                  arm_compile=arm_compile,
                  installed_sdk=sdk,
                  host_gain_benchmark=benchmark,
                  emulator_launched=False, physical_hardware=False, real_save_modified=False,
                  limits='Host adapter tests do not measure real DSP underrun duration, physical crackles, SD power-loss durability, ARM performance or a complete in-game save/reload.')
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'status': report['status'], 'tests': reports, 'arm_objects': len(arm_compile),
                      'host_gain_benchmark': benchmark, 'report': str(out / 'report.json')}, indent=2))


if __name__ == '__main__':
    main()
