"""Compile the exact prepared display sources with their configured ARM flags.

Requires a generated overlay already configured by the central build. Does not
patch sources or accept existing object files as evidence of the current input.
"""
import argparse
from pathlib import Path
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from devkitpro_tools import arm_tool


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'build/integrated-native/render60-source')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/integrated-native/options-display-probe')
    args = parser.parse_args()
    source = args.source.resolve()
    output = args.output.resolve()
    compiler = arm_tool('g++')
    units = [('gpu_composite.cpp', 'sm64ds_gpu_composite'),
             ('viewer_3ds.cpp', 'sm64ds_viewer'), ('display_culling.cpp', 'smoke_player')]
    commands = []
    for name, target in units:
        unit = source / 'port/platform/3ds' / name
        unit.read_bytes()  # Missing inputs fail before any compiler/output writes.
        flags = (source / f'build3ds/cmake/CMakeFiles/{target}.dir/flags.make').read_text()
        arguments = []
        for key in ['CXX_DEFINES', 'CXX_INCLUDES', 'CXX_FLAGS']:
            match = re.search(r'^' + key + r' = (.*)$', flags, re.M)
            if not match:
                raise ValueError('Missing configured ARM flag: ' + key)
            arguments += shlex.split(match[1])
        arguments += ['-I' + str(source / 'port/platform/3ds'), '-DSM64DS_GPU_COMPOSITE_CONFIG',
                      '-I' + str(source / 'build3ds/cmake/.dkp-generated/presentation'),
                      '-I' + str(source / 'build3ds/cmake/.dkp-generated/gpu_composite_shaders_bin')]
        commands.append((name, [compiler, *arguments, '-c', str(unit), '-o', str(output / (name + '.o'))]))
    output.mkdir(parents=True, exist_ok=True)
    for name, command in commands:
        result = subprocess.run(command, capture_output=True, text=True)
        (output / (name + '.log')).write_text(result.stdout + result.stderr)
        print(name, result.returncode)
        if result.returncode:
            print((result.stdout + result.stderr)[-6000:], file=sys.stderr)
        result.check_returncode()


if __name__ == '__main__':
    main()
