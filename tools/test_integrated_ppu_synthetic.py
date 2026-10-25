#!/usr/bin/env python3
"""Reproduce PPU differential gates without an emulator or private captures.

The two fixtures are authored register/tile/palette patterns. They exercise
the actual pinned PPU and derived implementations, not physical game output.
Their provenance is deliberately distinct from earlier captured-ROM fixtures.
"""
from pathlib import Path
import hashlib
import json
import shutil
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/integrated-native'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fixture(path, seed):
    path.mkdir(parents=True)
    registers, palette = bytearray(0x2000), bytearray(0x800)
    vram, oam = bytearray(0x900000), bytearray(0x800)

    def half(data, offset, value):
        struct.pack_into('<H', data, offset, value & 0xffff)

    for base in (0, 0x1000):
        struct.pack_into('<I', registers, base, 0x11b03 if base else 0x10100)
        for offset, value in ((8, 0x1800), (10, 0x1901), (14, 0x1c02),
                              (0x30, 256), (0x36, 256),
                              (0x50, 0x2840 if base else 0), (0x52, 0x0808)):
            half(registers, base + offset, value)
    for i in range(0, len(palette), 2):
        half(palette, i, 0x8000 | ((i * 73 + seed * 317 + 1) & 0x7fff))
    for bank in (0, 0x200000, 0x400000, 0x600000):
        for i in range(4096):
            vram[bank + i] = (i * 13 + i // 8 + seed * 19) % 255 if i % 4 else 0
        for screen in (0x1800, 0x1901, 0x1c02):
            start = bank + ((screen >> 8) & 31) * 0x800
            for i in range(1024):
                half(vram, start + i * 2, 1 + (i + seed) % 15)
    for i in range(256):
        half(oam, i * 8, 0x200)
    # Keep a semi-transparent object visible throughout the replay's scroll
    # variants, and include priority-two contacts for its admission boundaries.
    for i in range(16, 32):
        offset = 0x400 + i * 8
        half(oam, offset, 0x400 | 64)
        half(oam, offset + 2, 0x8000 | (i - 16) * 16)
        half(oam, offset + 4, 0x804)
    for number, data in ((4, registers), (5, palette), (6, vram), (7, oam)):
        (path / f'region-{number}.bin').write_bytes(data)


def check():
    from prepare_integrated_render60 import optimize_inactive_alpha, affine_alpha_source
    from test_integrated_inactive_alpha import sources as inactive_sources
    from test_integrated_affine_alpha import sources as affine_sources

    directory = OUT / ('ppu-synthetic-' + str(time.time_ns()))
    directory.mkdir(parents=True)
    port = ROOT / 'third_party/sm64ds-3ds/port'
    original = port / 'ntr/ppu.cpp'
    optimized, affine = directory / 'optimized.cpp', directory / 'affine.cpp'
    optimized.write_text(optimize_inactive_alpha(original.read_text()))
    affine.write_text(affine_alpha_source(original.read_text()))
    compiler = shutil.which('clang++') or shutil.which('c++')
    if not compiler:
        raise RuntimeError('A host C++ compiler is required for the PPU differential')

    def run(args, log):
        result = subprocess.run(list(map(str, args)), text=True, capture_output=True)
        log.write_text(result.stdout + result.stderr)
        result.check_returncode()
        return result.stdout

    binaries = {}
    for name, source, test in (
            ('original', original, 'tests/integrated_ppu_replay.cpp'),
            ('optimized', optimized, 'tests/integrated_ppu_replay.cpp'),
            ('affine', affine, 'tests/integrated_affine_alpha_replay.cpp')):
        binary = directory / name
        run([compiler, '-std=c++17', '-O2', '-g', '-D__3DS__',
             '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
             '-fno-omit-frame-pointer', '-I' + str(port / 'ntr/include'),
             '-I' + str(ROOT / 'source/integrated_native'), ROOT / test,
             source, port / 'ntr/vram_banks.cpp', port / 'ntr/bitmap.cpp',
             '-o', binary], directory / (name + '-build.log'))
        binaries[name] = binary

    inactive_results, affine_results = [], []
    for seed in (0, 1):
        inputs = directory / ('fixture-' + str(seed))
        fixture(inputs, seed)
        outputs = {}
        logs = {}
        for name, binary in binaries.items():
            output = directory / f'{seed}-{name}.pixels'
            logs[name] = run([binary, inputs, output], directory / f'{seed}-{name}.log')
            outputs[name] = output
        if outputs['original'].read_bytes() != outputs['optimized'].read_bytes():
            raise AssertionError(f'Inactive-alpha PPU output differs for synthetic fixture {seed}')
        files = {p.name: sha(p) for p in inputs.glob('region-*.bin')}
        inactive_results.append({'fixture': str(inputs), 'fixture_files': files,
                                 'output_sha256': sha(outputs['original']),
                                 'bytes': outputs['original'].stat().st_size,
                                 'original_log': logs['original'],
                                 'optimized_log': logs['optimized']})
        stats = json.loads(logs['affine'].splitlines()[-1])
        if stats['admitted'] != 192 or stats['refused'] != 25:
            raise AssertionError('Incomplete affine admission/rejection coverage')
        affine_results.append({'fixture': str(inputs), 'fixture_files': files,
                               'stats': stats, 'output_sha256': sha(outputs['affine']),
                               'bytes': outputs['affine'].stat().st_size})
    common = {'directory': str(directory), 'fixture_kind': 'authored_synthetic',
              'fixture_generator_sha256': sha(__file__), 'ASan': True, 'UBSan': True,
              'limits': 'Deterministic host PPU differential only; no captured-game, '
                        'emulator, GPU or physical acceptance and no FPS claim.'}
    inactive = {**common, 'status': 'pixel_exact_against_pinned_original', 'cases': 128,
                'sources': inactive_sources(), 'optimized_ppu_sha256': sha(optimized),
                'fixtures': inactive_results}
    affine_report = {**common, 'status': 'pixel_exact_against_full_CPU_PPU',
                     'sources': affine_sources(), 'derived_ppu_sha256': sha(affine),
                     'fixtures': affine_results}
    for filename, report in (('ppu-alpha-gate-latest.json', inactive),
                             ('affine-alpha-gate-latest.json', affine_report)):
        (OUT / filename).write_text(json.dumps(report, indent=2) + '\n')
        (directory / filename).write_text(json.dumps(report, indent=2) + '\n')
    print('PPU synthetic differential: 2 authored fixtures; 128 inactive-alpha cases; '
          '384 affine cases and 50 refusals; no emulator or physical acceptance.')
    return inactive, affine_report


if __name__ == '__main__':
    check()
