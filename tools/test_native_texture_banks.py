#!/usr/bin/env python3
"""Test the original GX bank/upload contracts against the supplied ASMP0 pack.

The fixture reads only verified header/ARM9 data and generates table constants
inside a temporary directory. No ROM bytes are written into repository source.
Executes real native VRAM, texture bridge and GX decoder implementations on the
host. Optional sanitizer and ARM11 compilation are separate CPU/build evidence;
none establishes gameplay, emulator behavior or physical 3DS performance.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'build/game-data/python'))
import ndspy.codeCompression

PIN = '6230b2996636e3fd0e03cb178161c94cb4b73cc9'
SOURCES = [
    'tests/test_native_texture_banks.cpp',
    'source/ctr/game_geometry.cpp',
    'source/core/game_geometry.cpp',
    'source/game_port/native_video_services.cpp',
    'source/game_port/native_video.cpp',
    'source/core/game_draw.cpp',
    'source/vendor/ntr/gx.cpp',
]
TABLES = [('data_02086314', 0x02086314, 8),
          ('data_02086324', 0x02086324, 46),
          ('data_02086326', 0x02086326, 46),
          ('data_02086328', 0x02086328, 46)]


def read_pack(path):
    raw = path.read_bytes()
    assert raw[:8] == b'SM64PAK1' and raw[16:21] == b'ASMP\0'
    assert struct.unpack_from('<I', raw, 28)[0] == 0x29715dec
    assert zlib.crc32(raw[:40]) == struct.unpack_from('<I', raw, 40)[0]
    count = struct.unpack_from('<I', raw, 12)[0]
    index = raw[64:64+272*count]
    assert len(index) == 272*count
    assert zlib.crc32(index) == struct.unpack_from('<I', raw, 36)[0]
    files = {}
    for at in range(0, len(index), 272):
        _, offset, size, crc = struct.unpack_from('<4I', index, at)
        name = index[at+16:at+272].split(b'\0')[0].decode()
        if name not in ('__rom/arm9.bin', '__rom/header.bin'):
            continue
        assert name not in files
        data = raw[offset:offset+size]
        assert len(data) == size and zlib.crc32(data) == crc
        files[name] = data
    header = files['__rom/header.bin']
    assert header[0x0c:0x10] == b'ASMP' and header[0x1e] == 0
    return (ndspy.codeCompression.decompress(files['__rom/arm9.bin']),
            struct.unpack_from('<I', header, 0x28)[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack', type=Path, default=ROOT/'build/europe.pak')
    parser.add_argument('--report', type=Path,
                        default=ROOT/'build/native-runtime/texture-bank-test.json')
    parser.add_argument('--compiler', default='c++')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--arm-compile', action='store_true')
    args = parser.parse_args()
    actual_pin = subprocess.check_output(['git', 'rev-parse', 'HEAD'],
                                         cwd=ROOT/'upstream', text=True).strip()
    assert actual_pin == PIN, 'SDK evidence pin changed'
    arm9, base = read_pack(args.pack)
    definitions = []
    for name, address, count in TABLES:
        values = struct.unpack_from('<'+'H'*count, arm9, address-base)
        definitions.append('std::uint16_t '+name+'['+str(count)+']={' +
                           ','.join(hex(value) for value in values)+'};')
    with tempfile.TemporaryDirectory(prefix='sm64ds-texture-bank-') as directory:
        temp = Path(directory)
        fixture = temp/'rom_tables.cpp'
        fixture.write_text('#include <cstdint>\nextern "C" {\n' +
                           '\n'.join(definitions)+'\n}\n')
        executable = temp/'texture-bank-contract'
        command = [args.compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                   '-UNDEBUG', '-I'+str(ROOT/'include')]
        if args.sanitize:
            command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-fno-omit-frame-pointer']
        subprocess.run([*command, *[str(ROOT/source) for source in SOURCES],
                        str(fixture), '-o', str(executable)], check=True, cwd=ROOT)
        environment = os.environ.copy()
        if args.sanitize:
            environment['UBSAN_OPTIONS'] = 'halt_on_error=1'
        checked = subprocess.run([str(executable)], check=True, text=True,
                                 capture_output=True, env=environment)
        assert checked.stdout.startswith('Host texture SDK contracts passed:')
        assert not checked.stderr, checked.stderr
        if args.arm_compile:
            compiler = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++'
            flags = ['-std=gnu++20', '-march=armv6k', '-mtune=mpcore',
                     '-mfloat-abi=hard', '-mtp=soft', '-mword-relocations',
                     '-ffunction-sections', '-D__3DS__', '-Wall', '-Wextra',
                     '-Werror', '-I'+str(ROOT/'include')]
            for source in ('source/ctr/game_geometry.cpp',
                           'source/game_port/native_video_services.cpp'):
                subprocess.run([compiler, *flags, '-c', str(ROOT/source),
                                '-o', str(temp/(Path(source).stem+'.o'))],
                               check=True, cwd=ROOT)
    report = {
        'status': 'passed', 'scope': 'host SDK contracts and optional ARM11 compilation',
        'upstream_commit': PIN, 'profile': 'ASMP0',
        'sanitizers': ['address', 'undefined'] if args.sanitize else [],
        'arm11_compilation': 'passed' if args.arm_compile else 'not requested',
        'emulator_verified': False, 'physical_hardware': False,
        'playability_verified': False, 'performance_verified': False,
        'texture_masks': list(range(16)),
        'palette_masks': [0, 0x10, 0x20, 0x30, 0x40, 0x60, 0x70],
        'unsupported_palette_masks': [0x50],
        'checks': checked.stdout.strip(),
        'tables_sha256': hashlib.sha256(arm9[0x02086314-base:0x02086384-base]).hexdigest(),
        'source_sha256': {source: hashlib.sha256((ROOT/source).read_bytes()).hexdigest()
                          for source in SOURCES},
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
