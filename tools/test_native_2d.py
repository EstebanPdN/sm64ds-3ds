#!/usr/bin/env python3
"""Compare optimized native 2D scanout pixel-for-pixel with its prior version.

Uses real native_video storage, local boot register/palette/VRAM and scanout
dumps, and generated BG/OBJ/window/blend fixtures. No ROM/dump bytes are stored
in source. Host timings are comparative diagnostics, never 3DS FPS evidence.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT/'tests/reference/native_2d_before_sort.cpp'
REFERENCE_SHA = 'd934c6a9658c4d9eba11bba71077bee4aaca0378bd3e0b858394eee169bd44c3'
BOOT = ROOT/'build/native-runtime-azahar/attempt-1791593448151098000'
SOURCES = ['tests/test_native_2d.cpp', 'source/game_port/native_2d.cpp',
           'source/game_port/native_video.cpp']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(compiler, temporary, reference, sanitize):
    flags = ['-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
             '-I'+str(ROOT/'include')]
    if sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                  '-fno-omit-frame-pointer']
    objects = []
    for index, source in enumerate([reference, *[ROOT/path for path in SOURCES]]):
        output = temporary/(('san-' if sanitize else '')+'source'+str(index)+'.o')
        specific = (['-Wno-unused-const-variable', '-Wno-misleading-indentation']
                    if index == 0 else [])
        subprocess.run([compiler, *flags, *specific, '-c', str(source), '-o', str(output)],
                       check=True, cwd=ROOT)
        objects.append(str(output))
    executable = temporary/('scanout-sanitize' if sanitize else 'scanout')
    subprocess.run([compiler, *flags, *objects, '-o', str(executable)],
                   check=True, cwd=ROOT)
    return executable


def check(executable, args, blend_off, benchmark=0):
    environment = os.environ.copy()
    environment['SM64DS_BLEND_OFF'] = '1' if blend_off else '0'
    environment['SM64DS_OAMAGE_TRACE'] = '0'
    environment['UBSAN_OPTIONS'] = 'halt_on_error=1'
    command = [str(executable), str(args.boot_video), str(args.boot_scanout)]
    if benchmark:
        command.append(str(benchmark))
    result = subprocess.run(command, check=False, text=True, capture_output=True,
                            env=environment, cwd=ROOT)
    if result.returncode:
        raise RuntimeError('compositor test failed:\n'+result.stdout+result.stderr)
    assert not result.stderr, result.stderr
    payload = json.loads(result.stdout)
    assert payload['status'] == 'passed'
    return payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='c++')
    parser.add_argument('--boot-video', type=Path, default=BOOT/'boot-video.bin')
    parser.add_argument('--boot-scanout', type=Path, default=BOOT/'boot-scanout.bin')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--arm-compile', action='store_true')
    parser.add_argument('--benchmark', type=int, default=0,
                        help='host median samples per compositor; compiled without sanitizers')
    parser.add_argument('--report', type=Path, default=ROOT/'build/native-runtime/native-2d-test.json')
    args = parser.parse_args()
    assert 0 <= args.benchmark <= 2000
    assert sha(REFERENCE) == REFERENCE_SHA, 'the prior compositor reference changed'
    original = REFERENCE.read_text()
    assert original.count('namespace sm64ds {') == 1
    with tempfile.TemporaryDirectory(prefix='sm64ds-native-2d-') as directory:
        temporary = Path(directory)
        reference = temporary/'reference.cpp'
        reference.write_text(original.replace('namespace sm64ds {',
                                              'namespace sm64ds::reference {', 1))
        executable = build(args.compiler, temporary, reference, args.sanitize)
        results = [check(executable, args, False), check(executable, args, True)]
        timing = None
        if args.benchmark:
            if args.sanitize:
                executable = build(args.compiler, temporary, reference, False)
            timing = check(executable, args, False, args.benchmark)
        if args.arm_compile:
            subprocess.run(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++',
                '-std=gnu++20', '-O2', '-march=armv6k', '-mtune=mpcore',
                '-mfloat-abi=hard', '-mtp=soft', '-mword-relocations',
                '-ffunction-sections', '-D__3DS__', '-Wall', '-Wextra', '-Werror',
                '-I'+str(ROOT/'include'), '-c', str(ROOT/'source/game_port/native_2d.cpp'),
                '-o', str(temporary/'native_2d_arm.o')], check=True, cwd=ROOT)
    report = {
        'status': 'passed', 'scope': 'host exact pixel comparison and optional ARM11 compilation',
        'reference_sha256': REFERENCE_SHA,
        'source_sha256': {path: sha(ROOT/path) for path in SOURCES},
        'boot_video': str(args.boot_video), 'boot_video_sha256': sha(args.boot_video),
        'boot_scanout': str(args.boot_scanout), 'boot_scanout_sha256': sha(args.boot_scanout),
        'checks': results, 'host_comparative_timing': timing,
        'host_timing_samples': args.benchmark,
        'sanitizers': ['address', 'undefined'] if args.sanitize else [],
        'arm11_compilation': 'passed' if args.arm_compile else 'not requested',
        'emulator_run': False, 'physical_hardware': False,
        'playability_verified': False, 'target_performance_verified': False,
        'known_unchanged_limits': ['main BG extended palette F/G routing',
            'bitmap OBJ partial alpha', 'scanline-dependent effects', '3D blend with BG/OBJ'],
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
