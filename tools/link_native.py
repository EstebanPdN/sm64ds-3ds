#!/usr/bin/env python3
"""Reproduce the original-game ARM11 link frontier, never label it executable.

Generate BSS from the unadapted frontier first, then apply explicit native
providers. Keeping those passes separate avoids silently losing BSS aliases
when a previous run's adapted inventory is used as input.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OBJECTS = [
    'build/native-globals/game_bss.o',
    'build/3ds/source/ctr/CMakeFiles/sm64ds_game_math.dir/game_math.o',
    'build/3ds/CMakeFiles/sm64ds_math.dir/source/core/fixed_math.o',
    'build/3ds/source/ctr/CMakeFiles/sm64ds_game_primitives.dir/game_primitives.o',
    'build/3ds/source/ctr/CMakeFiles/sm64ds_game_primitives.dir/game_memory_primitives.o',
    'build/3ds/CMakeFiles/sm64ds_game_data.dir/source/game_data/native_constants.o',
    'build/3ds/CMakeFiles/sm64ds_game_data.dir/source/game_data/game_data.o',
    'build/3ds/CMakeFiles/sm64ds_assets.dir/source/core/assets.o',
    'build/3ds/CMakeFiles/sm64ds_game_data.dir/source/game_data/relocation.o',
    'build/3ds/CMakeFiles/sm64ds_game_data.dir/source/game_data/menu_callbacks.o',
    'build/3ds/CMakeFiles/sm64ds_game_files.dir/source/ctr/game_files.o',
    'build/3ds/CMakeFiles/sm64ds_game_files.dir/source/ctr/game_file_bridge.o',
    'build/3ds/CMakeFiles/sm64ds_game_files.dir/source/core/narc.o',
    'build/3ds/CMakeFiles/sm64ds_models.dir/source/vendor/ntr/lz77.o',
    'build/3ds/CMakeFiles/sm64ds_game_input.dir/source/core/game_input.o',
    'build/3ds/source/ctr/CMakeFiles/sm64ds_game_input_bridge.dir/game_input.o',
    'build/native-adapters/original_check_input.o',
    'build/3ds/source/ctr/CMakeFiles/sm64ds_game_symbols.dir/game_symbols.o',
]
REPLACEMENTS = [
    'src/_ZN4cstd3divEii.cpp', 'src/_ZN4cstd4fdivEii.cpp',
    'src/_ZN4cstd10fdiv_asyncE5Fix12IiE5Fix12IiE.c',
    'src/_ZN4cstd11fdiv_resultEv.cpp', 'src/_ZN4cstd4ldivEii.cpp',
    'src/_ZN4cstd11ldiv_resultEv.cpp', 'src/_ZN4cstd4sqrtEy.cpp',
    'src/func_02053274.c', 'src/NormalizeVec3.c', 'src/func_02049018.c',
    'src/_ZN13SharedFilePtr4LoadEv.cpp',
    'src/_ZN5Stage10CheckInputEv.cpp',
]


def run(*args):
    subprocess.run([str(x) for x in args], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--compile-core', action='store_true',
                        help='Refresh the audited original-source compile first')
    args = parser.parse_args()
    if not 1 <= args.jobs <= 32:
        parser.error('--jobs must be between 1 and 32')
    if args.compile_core:
        # 1 means the compiler audit contains identified failures; those
        # remain visible and are not silently turned into stub definitions.
        result = subprocess.run([sys.executable, 'tools/native_core.py',
                                 '--jobs', str(args.jobs)], cwd=ROOT)
        if result.returncode not in (0, 1):
            raise SystemExit(result.returncode)
    out = ROOT / 'build/native-core'
    if not (out/'report.json').is_file():
        parser.error('Run tools/native_core.py, or pass --compile-core')
    run('cmake', '--build', 'build/3ds', '--parallel', args.jobs)
    run(sys.executable, 'tools/native_core_inventory.py', '--link-frontier')
    baseline = out/'baseline'
    baseline.mkdir(exist_ok=True)
    for name in ('counts.json', 'symbols.json', 'frontier-link.txt'):
        shutil.copyfile(out/name, baseline/name)
    run(sys.executable, 'tools/game_globals.py', '--frontier', baseline/'symbols.json')
    report = json.loads((out/'report.json').read_text())
    entry = next(e for e in report['entries']
                 if e['source']=='src/_ZN5Stage10CheckInputEv.cpp')
    if not entry['compiled']:
        raise RuntimeError('Original input method did not compile')
    adapter = ROOT/'build/native-adapters'
    adapter.mkdir(exist_ok=True)
    objcopy = Path(os.environ.get('DEVKITPRO','/opt/devkitpro'))/'devkitARM/bin/arm-none-eabi-objcopy'
    run(objcopy, '--redefine-sym', '_ZN5Stage10CheckInputEv=sm64ds_original_check_input',
        entry['object'], adapter/'original_check_input.o')
    command = [sys.executable, 'tools/native_core_inventory.py', '--link-frontier',
               '--root-symbol', 'sm64ds_prepare_game_data']
    for obj in OBJECTS:
        command += ['--extra-object', obj]
    for source in REPLACEMENTS:
        command += ['--replace-source', source]
    run(*command)
    counts = json.loads((out/'counts.json').read_text())
    print('Original-game relocatable link verified; this is NOT the CIA executable.')
    print(f"Still unresolved: {counts['playable_unresolved']}; "
          f"DS I/O source files requiring adaptation: {counts['playable_literal_ds_io_sources']}")


if __name__ == '__main__':
    main()
