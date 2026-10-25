#!/usr/bin/env python3
"""Compare native LenVec3 ARM11 instructions, exact ROM bytes and Python isqrt.

Only the original-leaf oracle provides DS sqrt/IME MMIO. Native execution has
no DS I/O mapping. This is a CPU/ABI test, not a 3DS emulator, game frame or FPS
measurement. Baselines run original ROM and compiled original source without
DS I/O and must fail at the actual register access.
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
    UC_MEM_READ_UNMAPPED, UC_ERR_READ_UNMAPPED)
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
    UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR)

ROOT = Path(__file__).resolve().parents[1]
TOOL = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
OUT = ROOT/'build/native-lenvec3'
ORIGINAL, ORIGINAL_SIZE = 0x020536e4, 0x8c
BUFFER, STACK, STOP = 0x500000, 0x600000, 0x700000
DS = 0x04000000
MASK32, MASK64 = (1 << 32)-1, (1 << 64)-1
ARGS = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)
SAVED = (UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
         UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11)
SOURCES = ('source/ctr/game_math.cpp', 'source/core/fixed_math.cpp',
           'tests/native_len_vec3_arm.cpp', 'include/sm64ds/fixed_math.hpp',
           'upstream/src/LenVec3.c', 'tools/test_native_len_vec3_arm.py')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def signed32(value):
    bits = value & MASK32
    return bits-(1 << 32) if bits >> 31 else bits


def reference(vector):
    # Independent, unbounded Python squares. Narrow only at the original
    # register boundaries: unsigned 64-bit square sum/shift, then 32-bit add.
    squared = sum(value*value for value in vector) & MASK64
    radicand = (squared*4) & MASK64
    root = math.isqrt(radicand)
    return signed32((root+1) & MASK32) // 2, radicand, root


def symbols(path):
    return {parts[2]: int(parts[0], 16) for line in subprocess.check_output(
        [TOOL+'nm', '--defined-only', str(path)], text=True).splitlines()
        if len(parts := line.split()) == 3}


class Cpu:
    def __init__(self):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_ARM);self.pages = set()
        self.reads = [];self.writes = [];self.invalid = []
        for address, size, perms in ((BUFFER, 0x2000, 3), (STACK, 0x10000, 3), (STOP, 4096, 5)):
            self.map(address, size, perms)
        self.uc.hook_add(UC_HOOK_MEM_READ,
            lambda uc, access, address, size, value, user:self.reads.append((address, size)))
        self.uc.hook_add(UC_HOOK_MEM_WRITE,
            lambda uc, access, address, size, value, user:self.writes.append((address, size, value)))
        self.uc.hook_add(UC_HOOK_MEM_INVALID, self.unmapped)

    def map(self, address, size, perms=7):
        for page in range(address & ~4095, (address+size+4095) & ~4095, 4096):
            if page not in self.pages:
                self.uc.mem_map(page, 4096, perms);self.pages.add(page)

    def unmapped(self, uc, access, address, size, value, user):
        self.invalid.append(dict(access=access, address=address, size=size,
                                 pc=uc.reg_read(UC_ARM_REG_PC)))
        return False

    def elf(self, data):
        phoff = struct.unpack_from('<I', data, 28)[0]
        phsize, phnum = struct.unpack_from('<HH', data, 42)
        for i in range(phnum):
            kind, offset, address, _, filesz, memsz, _, _ = struct.unpack_from('<8I', data, phoff+i*phsize)
            if kind == 1 and memsz:
                self.map(address, memsz);self.uc.mem_write(address, data[offset:offset+filesz])

    def call(self, address, vector, offset, canary, alias=False):
        pointer = BUFFER+offset
        image = bytearray(canary);struct.pack_into('<3i', image, 32, *vector)
        self.uc.mem_write(pointer-32, bytes(image))
        self.uc.reg_write(UC_ARM_REG_CPSR, 0x10 | ((offset & 15) << 28))
        for reg, value in zip(ARGS, (pointer, 0x197ac35e, 0xe961bc07, 0x835dac01)):
            self.uc.reg_write(reg, value)
        self.uc.reg_write(UC_ARM_REG_SP, STACK+0x8000);self.uc.reg_write(UC_ARM_REG_LR, STOP)
        for i, reg in enumerate(SAVED):
            self.uc.reg_write(reg, 0x76540000+i)
        self.reads.clear();self.writes.clear();self.invalid.clear()
        self.uc.emu_start(address, STOP, count=20000)
        result = signed32(self.uc.reg_read(UC_ARM_REG_R0))
        assert self.uc.reg_read(UC_ARM_REG_PC) == STOP, 'Leaf did not return'
        assert self.uc.reg_read(UC_ARM_REG_SP) == STACK+0x8000, 'SP changed'
        assert all(self.uc.reg_read(reg) == 0x76540000+i for i, reg in enumerate(SAVED)), 'ABI r4-r11 changed'
        expected = bytearray(image)
        if alias:struct.pack_into('<i', expected, 32, result)
        assert bytes(self.uc.mem_read(pointer-32, len(image))) == expected, 'Vector/canary changed'
        source_reads = [(address, size) for address, size in self.reads
                        if BUFFER <= address < BUFFER+0x2000]
        touched = {byte for address, size in source_reads for byte in range(address, address+size)}
        assert touched == set(range(pointer, pointer+12)), ('Input read extent', source_reads)
        source_writes = [(address, size) for address, size, _ in self.writes
                         if BUFFER <= address < BUFFER+0x2000]
        assert source_writes == ([(pointer, 4)] if alias else []), ('Input write extent', source_writes)
        assert not self.invalid
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, default=OUT/'report.json')
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    source_hashes = {source:sha((ROOT/source).read_bytes()) for source in SOURCES}
    flags = ['-std=c++17', '-marm', '-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard',
             '-mtp=soft', '-Os', '-fno-strict-aliasing', '-ffunction-sections', '-fdata-sections',
             '-Wa,--noexecstack', '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'include')]
    objects = []
    for source in SOURCES[:3]:
        obj = OUT/(Path(source).stem+'.o')
        subprocess.run([TOOL+'g++', *flags, '-c', str(ROOT/source), '-o', str(obj)], check=True)
        objects.append(obj)
    elf_path = OUT/'lenvec3.elf'
    subprocess.run([TOOL+'g++', *flags, '-nostartfiles',
        '-Wl,--gc-sections,-z,noexecstack,-Ttext=0x100000,-e,LenVec3',
        '-Wl,-u,native_test_lenvec3_store_first', *map(str, objects), '-o', str(elf_path)], check=True)
    entries = symbols(elf_path);elf = elf_path.read_bytes()

    # Compile the original C body separately. Its first MMIO must remain a
    # blocked access without a DS page, never a result manufactured by native.
    baseline_obj = OUT/'original_lenvec3.o';baseline_elf = OUT/'original-lenvec3.elf'
    subprocess.run([TOOL+'gcc', *[flag for flag in flags if flag != '-std=c++17'],
        '-fwrapv', '-c', str(ROOT/'upstream/src/LenVec3.c'), '-o', str(baseline_obj)], check=True)
    subprocess.run([TOOL+'ld', '--gc-sections', '-z', 'noexecstack', '-Ttext=0x100000',
        '-e', 'LenVec3', str(baseline_obj), '-o', str(baseline_elf)], check=True)

    sys.path.insert(0, str(ROOT/'build/game-data/python'))
    import ndspy.rom
    import ndspy.codeCompression
    raw = (ROOT/'build/roms/europe.nds').read_bytes()
    assert zlib.crc32(raw) == 0x29715dec, 'Requires verified ASMP0 supplied ROM'
    rom = ndspy.rom.NintendoDSRom(raw)
    arm9 = ndspy.codeCompression.decompress(rom.arm9)
    code = bytes(arm9[ORIGINAL-rom.arm9RamAddress:ORIGINAL-rom.arm9RamAddress+ORIGINAL_SIZE])
    assert len(code) == ORIGINAL_SIZE
    (OUT/'original-rom-lenvec3.bin').write_bytes(code)

    baselines = {}
    for kind in ('rom', 'compiled_original'):
        cpu = Cpu()
        if kind == 'rom':
            cpu.map(ORIGINAL, ORIGINAL_SIZE, 5);cpu.uc.mem_write(ORIGINAL, code);address = ORIGINAL
        else:
            cpu.elf(baseline_elf.read_bytes());address = symbols(baseline_elf)['LenVec3']
        try:
            cpu.call(address, (3, 4, 0), 64, b'\xa5'*76)
            raise AssertionError('Original MMIO baseline unexpectedly returned')
        except UcError as error:
            assert error.errno == UC_ERR_READ_UNMAPPED
            assert len(cpu.invalid) == 1
            access = cpu.invalid[0]
            assert access['access'] == UC_MEM_READ_UNMAPPED and access['address'] == DS+0x208 and access['size'] == 2
            baselines[kind] = dict(blocked=True, first_access=access)
        assert DS not in cpu.pages

    native, original = Cpu(), Cpu()
    for cpu in (native, original):cpu.elf(elf)
    original.map(ORIGINAL, ORIGINAL_SIZE, 5);original.uc.mem_write(ORIGINAL, code)
    original.map(DS, 4096, 3)
    current = {};ime_values = (0, 1, 0xffff, 0x5312)
    def arithmetic(uc, access, address, size, value, user):
        if address == DS+0x2b4:
            mode = struct.unpack('<H', uc.mem_read(DS+0x2b0, 2))[0]
            parameter = struct.unpack('<Q', uc.mem_read(DS+0x2b8, 8))[0]
            assert mode == 1 and parameter == current['radicand']
            uc.mem_write(address, struct.pack('<I', math.isqrt(parameter)))
    original.uc.hook_add(UC_HOOK_MEM_READ, arithmetic, begin=DS+0x2b4, end=DS+0x2b4)
    # The same ARM11 ABI wrapper calls the ROM leaf in the oracle CPU. The
    # hook redirects at entry only and keeps the real LR/stack argument state.
    redirect = original.uc.hook_add(UC_HOOK_CODE,
        lambda uc, address, size, user:uc.reg_write(UC_ARM_REG_PC, ORIGINAL),
        begin=entries['LenVec3'], end=entries['LenVec3'])
    assert DS not in native.pages
    rng = random.Random(0x020536e4)
    edges = (0, 1, -1, 3, 4, 4096, -4096, 0x3fffffff, 0x40000000,
             -0x40000000, 0x7ffffffe, 0x7fffffff, -0x7fffffff, -0x80000000)
    vectors = list(itertools.product(edges, repeat=3))
    vectors += [tuple(signed32(rng.getrandbits(32)) for _ in range(3)) for _ in range(2000)]
    # Tight neighborhoods of square roots and sign/wrap thresholds complement
    # the full-width random input and all combinations of scalar boundaries.
    for center in (0, 4096, 0x3fffffff, 0x40000000, 0x40000001, 0x7fffffff):
        vectors += [(signed32(center+delta), second, third) for delta in range(-3, 4)
                    for second, third in ((0, 0), (1, 0), (1, 1), (3, 4))]
    offsets = (64, 68, 72, 76, 4096-12, 4096-8, 4096-4, 4096)
    alias_count = 0;negative_count = 0;wrapped_shift = 0;examples = []
    for index, vector in enumerate(vectors):
        want, radicand, root = reference(vector);current['radicand'] = radicand
        offset = offsets[index % len(offsets)];canary = rng.randbytes(76)
        ime = ime_values[index % len(ime_values)]
        original.uc.mem_write(DS+0x208, struct.pack('<H', ime))
        native_result = native.call(entries['LenVec3'], vector, offset, canary)
        original_result = original.call(ORIGINAL, vector, offset, canary)
        assert native_result == original_result == want, (vector, native_result, original_result, want)
        assert struct.unpack('<H', original.uc.mem_read(DS+0x208, 2))[0] == ime
        assert not any(DS <= address < DS+4096 for address, _ in native.reads)
        assert not any(DS <= address < DS+4096 for address, _, _ in native.writes)
        mmio_writes = [(address, size) for address, size, _ in original.writes if DS <= address < DS+4096]
        assert mmio_writes == [(DS+0x208, 2), (DS+0x2b0, 2), (DS+0x2b8, 4),
                               (DS+0x2bc, 4), (DS+0x208, 2)], mmio_writes
        if index % 16 == 0:
            native_result = native.call(entries['native_test_lenvec3_store_first'], vector, offset, canary, True)
            original_result = original.call(entries['native_test_lenvec3_store_first'], vector, offset, canary, True)
            assert native_result == original_result == want;alias_count += 1
        negative_count += want < 0
        wrapped_shift += sum(value*value for value in vector) > MASK64//4
        if len(examples) < 8 and (want < 0 or vector in ((0, 0, 0), (3, 4, 0))):
            examples.append(dict(vector=vector, result=want, radicand=hex(radicand), floor_root=root))
    assert negative_count and wrapped_shift
    assert source_hashes == {source:sha((ROOT/source).read_bytes()) for source in SOURCES}, 'Sources changed during test'
    report = dict(status='passed', scope='bounded ROM/ARM11 CPU leaf and ABI; no game execution',
        original_address=hex(ORIGINAL), original_size=ORIGINAL_SIZE, original_code_sha256=sha(code),
        rom_sha256=sha(raw), native_elf_sha256=sha(elf), native_sources_sha256=source_hashes,
        comparisons=len(vectors), alias_caller_comparisons=alias_count,
        signed_negative_results=negative_count, shifted_radicand_wrap_cases=wrapped_shift,
        input_canaries='76-byte windows unchanged; alias caller changes only first component after return',
        read_extent='exactly the 12 input bytes, including vectors crossing page boundaries',
        ABI='SP and r4-r11 preserved on every native/ROM call',
        native_ds_mmio_mapped=False, native_ds_mmio_accesses=0,
        oracle_mmio='original IME stores/restore, mode1 sqrt and radicand observed; only SQRT_RESULT injected',
        baselines=baselines, examples=examples, platform_profile='ARMv6K native instruction subset in Unicorn',
        azahar_executed=False, physical_hardware=False, title_reachability_verified=False,
        gameplay_verified=False, performance_verified=False)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({key:report[key] for key in ('status','comparisons','alias_caller_comparisons',
        'signed_negative_results','shifted_radicand_wrap_cases','native_ds_mmio_accesses','baselines')}, indent=2))
    print('Report:', args.report)


if __name__ == '__main__':
    main()
