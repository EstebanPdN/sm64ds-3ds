#!/usr/bin/env python3
"""Compare short-vector native leaves with exact ROM instructions and Python.

DS arithmetic MMIO exists only in the original ROM oracle. Native runs with
that page unmapped. Includes partial overlap, whose ROM component read/store
order differs from a snapshot-based normalization. This is a bounded CPU/ABI
test; it does not launch a game, Azahar or physical hardware.
"""
from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import sys
import zlib

from unicorn import (Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE,
                     UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_INVALID,
                     UC_ERR_READ_UNMAPPED, UC_ERR_WRITE_UNMAPPED)
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
    UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR)

ROOT = Path(__file__).resolve().parents[1]
TOOL = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
OUT = ROOT/'build/native-short-vector'
BUFFER, STACK, STOP, DS = 0x500000, 0x600000, 0x700000, 0x04000000
MASK64 = (1 << 64)-1
SAVED = (UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
         UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11)
ORIGINAL = {'func_02053644': (0x02053644, 0xa0),
            'func_02053380': (0x02053380, 0x16c)}
SOURCES = ('source/ctr/game_math.cpp', 'source/core/fixed_math.cpp',
           'tests/native_short_vector_arm.cpp', 'include/sm64ds/fixed_math.hpp',
           'upstream/src/func_02053644.c', 'upstream/src/func_02053380.c',
           'tools/test_native_short_vector_arm.py')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def symbols(path):
    return {p[2]: int(p[0], 16) for row in subprocess.check_output(
        [TOOL+'nm', '--defined-only', str(path)], text=True).splitlines()
        if len(p := row.split()) == 3}


def reference(image, start, displacement, normalize, alias=False):
    result = bytearray(image)
    initial = struct.unpack_from('<3h', result, start)
    squared = sum(x*x for x in initial)
    root = math.isqrt(squared*4)
    length = (root+1)//2
    if normalize:
        reciprocal = ((1 << 56)//squared if squared else -1) & MASK64
        scale = (reciprocal*root) & MASK64
        for i in range(3):
            # Read from evolving memory, matching ROM halfword stores even
            # when a preceding destination component covers a later source.
            component = struct.unpack_from('<h', result, start+2*i)[0]
            rounded = (scale*component+(1 << 44)) & MASK64
            struct.pack_into('<H', result, start+displacement+2*i,
                             (rounded >> 45) & 0xffff)
    elif alias:
        struct.pack_into('<H', result, start, length & 0xffff)
    return length, bytes(result), squared*4


class Cpu:
    def __init__(self):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        self.pages, self.reads, self.writes, self.invalid = set(), [], [], []
        self.map(BUFFER, 0x3000, 3)
        self.map(STACK, 0x10000, 3)
        self.map(STOP, 4096, 5)
        self.uc.hook_add(UC_HOOK_MEM_READ, self.read)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.write)
        self.uc.hook_add(UC_HOOK_MEM_INVALID, self.unmapped)

    def map(self, address, size, perms=7):
        for page in range(address & ~4095, (address+size+4095) & ~4095, 4096):
            if page not in self.pages:
                self.uc.mem_map(page, 4096, perms)
                self.pages.add(page)

    def read(self, uc, access, address, size, value, user):
        self.reads.append((address, size))

    def write(self, uc, access, address, size, value, user):
        self.writes.append((address, size, value))

    def unmapped(self, uc, access, address, size, value, user):
        self.invalid.append(dict(access=access, address=hex(address), size=size,
                                 pc=hex(uc.reg_read(UC_ARM_REG_PC))))
        return False

    def elf(self, raw):
        phoff = struct.unpack_from('<I', raw, 28)[0]
        phsize, phnum = struct.unpack_from('<HH', raw, 42)
        for i in range(phnum):
            kind, offset, address, _, filesz, memsz, _, _ = struct.unpack_from(
                '<8I', raw, phoff+i*phsize)
            if kind == 1 and memsz:
                self.map(address, memsz)
                self.uc.mem_write(address, raw[offset:offset+filesz])

    def call(self, address, image, source_offset, displacement=32,
             normalize=False, alias=False, checked=True):
        source = BUFFER+source_offset
        window = source-32
        self.uc.mem_write(window, image)
        self.uc.reg_write(UC_ARM_REG_CPSR, 0x10 | ((source_offset & 15) << 28))
        for reg, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                              UC_ARM_REG_R3),
                             (source, source+displacement, 0xe961bc07, 0x835dac01)):
            self.uc.reg_write(reg, value)
        self.uc.reg_write(UC_ARM_REG_SP, STACK+0x8000)
        self.uc.reg_write(UC_ARM_REG_LR, STOP)
        for i, reg in enumerate(SAVED):
            self.uc.reg_write(reg, 0x76540000+i)
        self.reads.clear()
        self.writes.clear()
        self.invalid.clear()
        self.uc.emu_start(address, STOP, count=20000)
        if checked:
            assert self.uc.reg_read(UC_ARM_REG_PC) == STOP, 'Leaf did not return'
            assert self.uc.reg_read(UC_ARM_REG_SP) == STACK+0x8000
            assert all(self.uc.reg_read(reg) == 0x76540000+i
                       for i, reg in enumerate(SAVED)), 'Callee-saved ABI changed'
            assert not self.invalid
            reads = {byte for at, size in self.reads
                     if BUFFER <= at < BUFFER+0x3000
                     for byte in range(at, at+size)}
            assert reads == set(range(source, source+6)), ('Source read extent', reads)
            writes = {byte for at, size, _ in self.writes
                      if BUFFER <= at < BUFFER+0x3000
                      for byte in range(at, at+size)}
            target = set(range(source+displacement, source+displacement+6)) if normalize else (
                set(range(source, source+2)) if alias else set())
            assert writes == target, ('Destination write extent', writes, target)
        return self.uc.reg_read(UC_ARM_REG_R0), bytes(self.uc.mem_read(window, len(image)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, default=OUT/'report.json')
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    hashes = {name: sha((ROOT/name).read_bytes()) for name in SOURCES}
    flags = ['-std=c++17', '-marm', '-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard',
             '-mtp=soft', '-Os', '-fno-strict-aliasing', '-ffunction-sections', '-fdata-sections',
             '-Wa,--noexecstack', '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'include')]
    objects = []
    for name in SOURCES[:3]:
        path = OUT/(Path(name).stem+'.o')
        subprocess.run([TOOL+'g++', *flags, '-c', str(ROOT/name), '-o', str(path)], check=True)
        objects.append(path)
    elf_path = OUT/'short-vector.elf'
    subprocess.run([TOOL+'g++', *flags, '-nostartfiles',
        '-Wl,--gc-sections,-z,noexecstack,-Ttext=0x100000,-e,func_02053644',
        '-Wl,-u,func_02053380,-u,native_test_short_length_store_first',
        *map(str, objects), '-o', str(elf_path)], check=True)
    entries, elf = symbols(elf_path), elf_path.read_bytes()

    sys.path.insert(0, str(ROOT/'build/game-data/python'))
    import ndspy.rom
    import ndspy.codeCompression
    raw = (ROOT/'build/roms/europe.nds').read_bytes()
    assert zlib.crc32(raw) == 0x29715dec, 'Requires verified supplied ASMP0 ROM'
    rom = ndspy.rom.NintendoDSRom(raw)
    arm9 = ndspy.codeCompression.decompress(rom.arm9)
    code = {name: bytes(arm9[at-rom.arm9RamAddress:at-rom.arm9RamAddress+size])
            for name, (at, size) in ORIGINAL.items()}
    assert all(len(code[name]) == size for name, (_, size) in ORIGINAL.items())

    baselines = {}
    for name, (at, size) in ORIGINAL.items():
        obj, path = OUT/('original-'+name+'.o'), OUT/('original-'+name+'.elf')
        subprocess.run([TOOL+'gcc', *[f for f in flags if f != '-std=c++17'], '-fwrapv',
                        '-c', str(ROOT/'upstream/src'/(name+'.c')), '-o', str(obj)], check=True)
        subprocess.run([TOOL+'ld', '--gc-sections', '-z', 'noexecstack', '-Ttext=0x100000',
                        '-e', name, str(obj), '-o', str(path)], check=True)
        for kind in ('rom', 'compiled_original'):
            cpu = Cpu()
            if kind == 'rom':
                cpu.map(at, size, 5)
                cpu.uc.mem_write(at, code[name])
                address = at
            else:
                cpu.elf(path.read_bytes())
                address = symbols(path)[name]
            image = bytearray(bytes([0xa5])*128)
            struct.pack_into('<3h', image, 32, 3, 4, 5)
            try:
                cpu.call(address, bytes(image), 64, normalize=name == 'func_02053380')
                raise AssertionError('Original MMIO baseline unexpectedly returned')
            except UcError as error:
                assert error.errno in (UC_ERR_READ_UNMAPPED, UC_ERR_WRITE_UNMAPPED)
                assert len(cpu.invalid) == 1
                expected = DS+(0x280 if name == 'func_02053380' else 0x208)
                assert cpu.invalid[0]['address'] == hex(expected) and cpu.invalid[0]['size'] == 2
                baselines[name+'/'+kind] = dict(blocked=True, first_access=cpu.invalid[0])
            assert DS not in cpu.pages

    native, original = Cpu(), Cpu()
    native.elf(elf)
    original.elf(elf)
    for name, (at, size) in ORIGINAL.items():
        original.map(at, size, 5)
        original.uc.mem_write(at, code[name])
    original.map(DS, 4096, 3)
    current, arithmetic_observations = {}, {'sqrt': 0, 'divide': 0}
    def arithmetic(uc, access, address, size, value, user):
        if address == DS+0x2b4:
            mode = struct.unpack('<H', uc.mem_read(DS+0x2b0, 2))[0]
            radicand = struct.unpack('<Q', uc.mem_read(DS+0x2b8, 8))[0]
            assert mode == 1 and radicand == current['radicand']
            uc.mem_write(address, struct.pack('<I', math.isqrt(radicand)))
            arithmetic_observations['sqrt'] += 1
        elif address in (DS+0x2a0, DS+0x2a4):
            mode = struct.unpack('<H', uc.mem_read(DS+0x280, 2))[0]
            numerator, denominator = struct.unpack('<qq', uc.mem_read(DS+0x290, 16))
            assert mode == 2 and numerator == 1 << 56
            assert denominator == current['radicand']//4
            quotient = (1 << 56)//denominator if denominator else -1
            uc.mem_write(DS+0x2a0, struct.pack('<Q', quotient & MASK64))
            arithmetic_observations['divide'] += 1
    original.uc.hook_add(UC_HOOK_MEM_READ, arithmetic, begin=DS+0x2a0, end=DS+0x2b7)
    original.uc.hook_add(UC_HOOK_CODE,
        lambda uc, address, size, user: uc.reg_write(UC_ARM_REG_PC, ORIGINAL['func_02053644'][0]),
        begin=entries['func_02053644'], end=entries['func_02053644'])
    rng = random.Random(0x02053380)
    edges = (-32768, -32767, -4096, -4, -1, 0, 1, 3, 4096, 32766, 32767)
    vectors = list(itertools.product(edges, repeat=3))
    vectors += [tuple(rng.randint(-32768, 32767) for _ in range(3)) for _ in range(1000)]
    vectors += [(3, 4, 0), (3, 4, 5), (-3, -4, -5), (0, 0, 0)]
    displacements, offsets = (-4, -2, 0, 2, 4, 32), (64, 66, 68, 4090, 4092, 4094, 4096)
    scalar_count, normalize_count, alias_count, partial_differences = 0, 0, 0, 0
    overlap_example = None
    for index, vector in enumerate(vectors):
        image = bytearray(rng.randbytes(128))
        struct.pack_into('<3h', image, 32, *vector)
        image = bytes(image)
        offset = offsets[index % len(offsets)]
        want, expected, radicand = reference(image, 32, 32, False)
        current['radicand'] = radicand
        ime = (0, 1, 0xffff, 0x5312)[index % 4]
        original.uc.mem_write(DS+0x208, struct.pack('<H', ime))
        left = native.call(entries['func_02053644'], image, offset)
        right = original.call(ORIGINAL['func_02053644'][0], image, offset)
        assert left == right == (want, expected), ('Length', vector, left, right, want)
        scalar_count += 1
        if index % 16 == 0:
            want, expected, _ = reference(image, 32, 32, False, alias=True)
            left = native.call(entries['native_test_short_length_store_first'], image, offset, alias=True)
            right = original.call(entries['native_test_short_length_store_first'], image, offset, alias=True)
            assert left == right == (want, expected)
            alias_count += 1
        _, snapshot, _ = reference(image, 32, 32, True)
        for displacement in displacements:
            _, expected, _ = reference(image, 32, displacement, True)
            left = native.call(entries['func_02053380'], image, offset, displacement, normalize=True)
            right = original.call(ORIGINAL['func_02053380'][0], image, offset, displacement, normalize=True)
            assert left[1] == right[1] == expected, ('Normalize', vector, displacement,
                                                  left[1].hex(), right[1].hex(), expected.hex())
            normalize_count += 1
            got = struct.unpack_from('<3h', expected, 32+displacement)
            snap = struct.unpack_from('<3h', snapshot, 64)
            if displacement in (2, 4) and got != snap:
                partial_differences += 1
            if vector == (3, 4, 5) and displacement == 2:
                overlap_example = dict(input=list(vector), destination_offset_bytes=2,
                                       rom_and_native=list(got), snapshot_based=list(snap))
        assert struct.unpack('<H', original.uc.mem_read(DS+0x208, 2))[0] == ime
        assert not any(DS <= at < DS+4096 for at, _ in native.reads)
        assert not any(DS <= at < DS+4096 for at, _, _ in native.writes)
    assert DS not in native.pages and partial_differences and overlap_example
    assert hashes == {name: sha((ROOT/name).read_bytes()) for name in SOURCES}, 'Sources changed during test'
    report = dict(status='passed', scope='Bounded exact ROM/ARM11 leaf instructions and ABI; no game execution',
        length_comparisons=scalar_count, normalization_comparisons=normalize_count,
        scalar_alias_caller_comparisons=alias_count,
        overlap_displacements_bytes=list(displacements),
        partial_overlap_cases_distinct_from_snapshot=partial_differences,
        overlap_example=overlap_example,
        native_sources_sha256=hashes, native_elf_sha256=sha(elf), rom_sha256=sha(raw),
        original_code={name: dict(address=hex(ORIGINAL[name][0]), size=len(body), sha256=sha(body))
                       for name, body in code.items()},
        baselines=baselines, native_ds_mmio_mapped=False, native_ds_mmio_accesses=0,
        oracle_mmio='Observed ROM mode2 divider numerator/denominator and mode1 sqrt radicand; results injected only at reads',
        arithmetic_observations=arithmetic_observations,
        memory='Full 128-byte windows match Python and ROM, exact six-byte input/output extents including page crossings',
        ABI='SP and r4-r11 preserved on every native and ROM invocation',
        cartridge_bytes_written_to_disk=False, azahar_executed=False, physical_hardware=False,
        title_reachability_verified=False, gameplay_verified=False, performance_verified=False)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({name: report[name] for name in ('status', 'length_comparisons',
        'normalization_comparisons', 'scalar_alias_caller_comparisons',
        'partial_overlap_cases_distinct_from_snapshot', 'overlap_example', 'native_ds_mmio_accesses')}, indent=2))
    print('Report:', args.report)


if __name__ == '__main__':
    main()
