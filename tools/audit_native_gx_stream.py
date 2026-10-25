#!/usr/bin/env python3
"""Replay preserved native GX submissions and actual uploads on the host.

The snapshots come from NativeFrame before decode. Both the production GX
decoder and GameTextures resolver are used. A separate, explicitly synthetic
texture fixture isolates command framing; it never produces a game image.
No Azahar, full game execution, physical-console or FPS claim is made here.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    'tools/native_gx_stream_probe.cpp', 'source/vendor/ntr/gx.cpp',
    'source/vendor/ntr/texture.cpp', 'source/core/game_textures.cpp',
    'source/core/game_geometry.cpp',
]
HASHED_SOURCES = SOURCES + ['tools/audit_native_gx_stream.py', 'include/ntr/gx.h',
                          'include/sm64ds/game_geometry.hpp', 'include/sm64ds/game_textures.hpp']
FIELDS = ('texture_capacity', 'palette_capacity', 'texture_generation',
          'palette_generation', 'texture_uploads', 'palette_uploads',
          'texture_bytes', 'palette_bytes')
FLAGS = ('texture_transfer', 'palette_transfer', 'highlight_shading')
TEXTURE_BYTES, PALETTE_BYTES = 524288, 98304


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def encode_metadata(value):
    numbers = []
    for field in FIELDS:
        number = value[field]
        if type(number) is not int or not 0 <= number <= 0xffffffff:
            raise ValueError(f'Invalid snapshot integer {field}')
        numbers.append(number)
    for field in FLAGS:
        if type(value[field]) is not bool:
            raise ValueError(f'Invalid snapshot boolean {field}')
        numbers.append(int(value[field]))
    return struct.pack('<11I', *numbers)


def write_stream(path, words):
    path.write_bytes(b''.join(struct.pack('<2I', port, value) for port, value in words))


def self_tests(probe, temp, environment):
    # This small indexed texture fixture deliberately matches the observed
    # particle binding's addresses/dimensions. It is not an original upload.
    metadata = dict(zip(FIELDS, (0x40000, 0x1000, 1, 1, 1, 1, 64, 8)))
    metadata.update(zip(FLAGS, (False, False, False)))
    texels = bytearray(TEXTURE_BYTES)
    texels[0x39580:0x395c0] = b'\xe4' * 64
    palettes = bytearray(PALETTE_BYTES)
    palettes[0x5c0:0x5c8] = struct.pack('<4H', 0, 0x001f, 0x03e0, 0x7fff)
    texture_path, palette_path, meta_path = (temp / name for name in ('fixture-textures.bin',
                                            'fixture-palettes.bin', 'fixture-geometry.bin'))
    texture_path.write_bytes(texels);palette_path.write_bytes(palettes)
    meta_path.write_bytes(encode_metadata(metadata))
    triangle = [(0x40, 0), (0x24, 0), (0x24, 64), (0x24, 64 << 10), (0x41, 0)]
    bind = [(0x2a, 0x689072b0), (0x2b, 0xb8)]
    matrix = [4096, 0, 0, 0, 4096, 0, 0, 0, 4096, 0, 0, 0]
    cases = {
        'actual-format2-fixture': (bind + triangle, 'real', True, None),
        'untextured-triangle': (triangle, 'real', True, None),
        'complete-matrix-fifo': ([(0, 0x19)] + [(0, word) for word in matrix],
                                 'real', True, None),
        'pending-matrix-fifo': ([(0, 0x19)] + [(0, word) for word in matrix[:-1]],
                                'real', False, 'fifo_pending'),
        'pending-vtx16-port': ([(0x40, 0), (0x23, 0)], 'real', False, 'port_pending'),
        'supported-texgen2': ([(0x2a, 0x80000000), (0x22, 0x00010002),
                              (0x21, 511)] + triangle, 'real', True, None),
        'unsupported-texgen3': ([(0x2a, 0xc0000000)], 'real', False, 'texture_generation'),
        'unsupported-texture-push': ([(0x10, 3), (0x11, 0)], 'real', False, 'matrix_operation'),
        'unsupported-texture-pop': ([(0x10, 3), (0x12, 1)], 'real', False, 'matrix_operation'),
        'unsupported-texture-store-index': ([(0x10, 3), (0x13, 1)], 'real', False, 'matrix_operation'),
        'unsupported-texture-restore-index': ([(0x10, 3), (0x13, 0), (0x14, 1)],
                                            'real', False, 'matrix_operation'),
        'unknown-direct-command': ([(0x61, 0)], 'real', False, 'command'),
        'unimplemented-box-test': ([(0x70, 0)] * 3, 'real', False, 'command'),
        'latched-first-failure': ([(0x2a, 0x40000000), (0x2b, 12), (0x61, 0),
                                   (0x2a, 0xc0000000)], 'real', False, 'command'),
        'palette-outside-live-capacity': ([(0x2a, 0x689072b0), (0x2b, 0x1fff)] + triangle,
                                         'real', False, 'texture_resolution'),
        'same-stream-protocol-fixture': ([(0x2a, 0x689072b0), (0x2b, 0x1fff)] + triangle,
                                         'fixture', True, None),
    }
    results = {}
    for name, (words, mode, valid, expected_failure) in cases.items():
        stream = temp / (name + '.bin')
        write_stream(stream, words)
        result = subprocess.run([str(probe), str(stream), mode, str(meta_path),
                                 str(texture_path), str(palette_path)], check=True,
                                text=True, capture_output=True, env=environment)
        if result.stderr:
            raise RuntimeError(result.stderr)
        decoded = json.loads(result.stdout)
        assert decoded['stream_valid'] is valid, (name, decoded)
        assert decoded['reset_valid'] and decoded['reset_diagnostic']['failure'] == 'none'
        assert expected_failure is None or expected_failure in decoded['failure_kinds'], (name, decoded)
        if name == 'actual-format2-fixture':
            assert decoded['triangles'] == 1 and decoded['texture_queries'][0]['accepted']
            assert decoded['texture_queries'][0]['first_pixel_argb'] == '0x00000000'
        if name == 'latched-first-failure':
            assert decoded['first_failure_word'] == 2
            assert decoded['final_diagnostic']['failing_command'] == 0x61
            assert decoded['final_diagnostic']['texture_image'] == '0x40000000'
            assert decoded['final_diagnostic']['texture_palette'] == '0x0000000c'
        results[name] = decoded
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot-dir', type=Path,
                        help='Directory containing scene-1-first-{gx,textures,palettes}.bin and geometry.json')
    parser.add_argument('--prefix', default='scene-1-first')
    parser.add_argument('--report', type=Path, default=ROOT/'build/native-gx-audit/report.json')
    parser.add_argument('--compiler', default='c++')
    parser.add_argument('--no-sanitize', action='store_true')
    args = parser.parse_args()
    source_hashes = {source: digest(ROOT/source) for source in HASHED_SOURCES}
    inputs, snapshot_results = {}, None
    with tempfile.TemporaryDirectory(prefix='sm64ds-gx-audit-') as directory:
        temp = Path(directory)
        probe = temp/'gx-stream-probe'
        flags = ['-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                 '-I'+str(ROOT/'include')]
        if not args.no_sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                      '-fno-omit-frame-pointer']
        subprocess.run([args.compiler, *flags, *[str(ROOT/source) for source in SOURCES],
                        '-o', str(probe)], check=True, cwd=ROOT)
        environment = os.environ.copy()
        environment['UBSAN_OPTIONS'] = 'halt_on_error=1'
        controls = self_tests(probe, temp, environment)
        if args.snapshot_dir:
            files = {kind: args.snapshot_dir/(args.prefix+'-'+kind+extension)
                     for kind, extension in (('gx', '.bin'), ('textures', '.bin'),
                                             ('palettes', '.bin'), ('geometry', '.json'))}
            for kind, path in files.items():
                inputs[kind] = {'path': str(path.resolve()), 'bytes': path.stat().st_size,
                                'sha256': digest(path)}
            assert files['textures'].stat().st_size == TEXTURE_BYTES, 'Truncated texture snapshot'
            assert files['palettes'].stat().st_size == PALETTE_BYTES, 'Truncated palette snapshot'
            metadata = json.loads(files['geometry'].read_text())
            binary_meta = temp/'geometry.bin'
            binary_meta.write_bytes(encode_metadata(metadata))
            snapshot_results = {'geometry': metadata}
            for mode in ('fixture', 'real'):
                result = subprocess.run([str(probe), str(files['gx']), mode, str(binary_meta),
                                         str(files['textures']), str(files['palettes'])], check=True,
                                        text=True, capture_output=True, env=environment)
                if result.stderr:
                    raise RuntimeError(result.stderr)
                snapshot_results[mode] = json.loads(result.stdout)
        assert source_hashes == {source: digest(ROOT/source) for source in HASHED_SOURCES}, \
            'Production source changed during replay; rebuild with the matching sources'
    report = {
        'status': 'replay completed' if snapshot_results else 'diagnostic controls passed',
        'scope': 'host GX decoder and uploaded-texture resolver; no image rasterized',
        'sanitizers': [] if args.no_sanitize else ['address', 'undefined'],
        'emulator_executed_by_tool': False, 'physical_hardware': False,
        'gameplay_verified': False, 'performance_verified': False,
        'protocol_fixture': 'explicit synthetic texture tokens for command framing only',
        'self_tests': controls, 'snapshot': snapshot_results, 'inputs': inputs,
        'source_sha256': source_hashes,
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({'status': report['status'], 'self_tests': len(controls),
                      'snapshot': snapshot_results, 'report': str(args.report)}, indent=2))


if __name__ == '__main__':
    main()
