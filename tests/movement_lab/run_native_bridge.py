#!/usr/bin/env python3
"""Production motion bridge + real headless core host regression, no emulator.

Builds its own generated core with tools/movement_lab/build_core.py so another
build in the shared checkout cannot mutate this test's sources. ASan/UBSan cover
the bridge, DS/HID stand-ins, ownership headers, headless entry and platform
displacement TU. Other archive members retain their ordinary host flags.
"""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    out = ROOT / 'build/movement-adapter' / ('native-bridge-' + str(time.time_ns()))
    out.mkdir(parents=True)
    # Keep the existing generator and source pin checks, with per-run outputs.
    # These module settings are private to this Python process; no pipeline
    # source or shared generated core is changed.
    sys.path.insert(0, str(ROOT / 'tools/movement_lab'))
    import build_core
    core = out / 'core'
    build_core.OUT = core
    build_core.LIB = core / 'source'
    build_core.build(False)
    commands = []
    evidence = json.loads((core / 'host/sources.json').read_text())
    for name, digest in evidence.items():
        assert hashlib.sha256(Path(name).read_bytes()).hexdigest() == digest, name
    for name in ('solver_contacts.h', 'solver_contacts.inc', 'collision_provider.h', 'collision_provider.c'):
        assert (core / 'source/src' / name).read_bytes() == (ROOT / 'source/integrated_native/movement' / name).read_bytes(), name
    includes = ['-I' + str(p) for p in (
        ROOT / 'tests/movement_lab/native_bridge_stubs',
        ROOT / 'source/integrated_native/movement', core / 'source/src',
        core / 'source/src/decomp/include')]
    flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    defines = ['-DGBI_FLOATS', '-DVERSION_US', '-DNO_SEGMENTED_MEMORY']

    def run(command):
        command = list(map(str, command))
        commands.append(command)
        result = subprocess.run(command, capture_output=True, text=True, timeout=120)
        with (out / 'run.log').open('a') as log:
            log.write(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(f'Command failed ({result.returncode}): {command}\n{result.stdout[-4000:]}{result.stderr[-4000:]}')
        return result.stdout.strip()

    for name, source in (
        ('headless', core / 'headless.c'),
        ('platform', core / 'source/src/decomp/game/platform_displacement.c'),
        ('fixture', ROOT / 'tests/movement_lab/solver_contacts_fixture.c')):
        run(['clang', '-std=gnu11', *flags, *defines, *includes,
             '-Wno-incompatible-pointer-types', '-Wno-int-conversion', '-c', source, '-o', out / (name + '.o')])
    outputs = []
    for name in ('test_native_bridge', 'test_world_session'):
        run(['clang++', '-std=c++17', *flags, *includes, ROOT / 'tests/movement_lab' / (name + '.cpp'),
             out / 'headless.o', out / 'platform.o', out / 'fixture.o', core / 'host/libmotion64.a', '-lm', '-o', out / name])
        outputs.append(run([out / name]))
    output = '\n'.join(outputs)
    sources = [*ROOT.joinpath('source/integrated_native/movement').glob('*'),
               ROOT / 'tools/prepare_integrated_movement.py',
               ROOT / 'tests/movement_lab/test_native_bridge.cpp', Path(__file__)]
    sources += [ROOT / 'tests/movement_lab/test_world_session.cpp', ROOT / 'tests/movement_lab/solver_contacts_fixture.c',
                ROOT / 'tools/movement_lab/build_core.py', ROOT / 'tools/movement_lab/port_source.py']
    sources += list(ROOT.joinpath('tests/movement_lab/native_bridge_stubs').rglob('*'))
    report = {'status': 'passed', 'output': output, 'commands': commands,
              'sources': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources if p.is_file()},
              'core_sources': evidence,
              'scope': __doc__.strip()}
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(output)
    print(out / 'report.json')


if __name__ == '__main__':
    main()
