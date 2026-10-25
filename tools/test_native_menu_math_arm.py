#!/usr/bin/env python3
"""Compare OV007 native arithmetic leaves with the supplied ROM and Python.

The native CPU has no DS I/O mapping. Only the separate original-leaf oracle
models the original arithmetic unit. This is a bounded CPU/ABI test, with no
gameplay, emulator application, physical-console or FPS claim.
"""
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

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_READ
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
    UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR)

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'build/game-data/python'))
import ndspy.rom
import ndspy.codeCompression

OUT = ROOT / 'build/native-menu-math'
TOOL = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
NAMES = ('func_ov007_020c30ac', 'func_ov007_020c31e0',
         'func_ov007_020c368c', 'func_ov007_020c7d60')
OUT.mkdir(exist_ok=True)
flags = ['-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft',
         '-Os', '-fno-strict-aliasing', '-ffunction-sections', '-fdata-sections',
         '-Wa,--noexecstack', '-Werror=trampolines', '-I', str(ROOT / 'include')]
sources = ('source/game_port/native_menu_math.cpp', 'source/ctr/game_math.cpp',
           'source/core/fixed_math.cpp', 'tests/native_menu_math_failure_stub.cpp',
           'upstream/src/DotVec3.c', 'upstream/src/func_02053320.c')
objects = []
for source in sources:
    obj = OUT / (Path(source).stem + '.o')
    compiler = 'g++' if source.endswith('.cpp') else 'gcc'
    subprocess.run([TOOL + compiler, *flags, '-c', str(ROOT / source),
                    '-o', str(obj)], check=True)
    objects.append(obj)
elf_path = OUT / 'menu-math.elf'
roots = (*NAMES, 'ARMMathSaveState', 'ARMMathLoadState',
         '_ZN4cstd10fdiv_asyncE5Fix12IiE5Fix12IiE',
         '_ZN4cstd11fdiv_resultEv', '_ZN4cstd11ldiv_resultEv')
subprocess.run([TOOL + 'g++', *flags, '-nostartfiles',
    '-Wl,--gc-sections,-z,noexecstack,-Ttext=0x100000,-e,' + NAMES[0],
    *('-Wl,-u,' + name for name in roots), *map(str, objects),
    '-o', str(elf_path)], check=True)
symbols = {parts[2]: int(parts[0], 16) for line in subprocess.check_output(
    [TOOL + 'nm', '--defined-only', str(elf_path)], text=True).splitlines()
    if len(parts := line.split()) == 3}

BUFFER, STACK, STOP = 0x500000, 0x600000, 0x700000
MASK64, MASK32 = (1 << 64) - 1, (1 << 32) - 1
ARGS = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)
SAVED = (UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
         UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11)
CANARY = bytes([0xa5])


def s32(value):
    value &= MASK32
    return value - (1 << 32) if value >> 31 else value


def s64(value):
    value &= MASK64
    return value - (1 << 64) if value >> 63 else value


def s16(value):
    value &= 0xffff
    return value - 0x10000 if value >> 15 else value


def quotient(numerator, denominator):
    if not denominator:
        return -1 if numerator >= 0 else 1
    return s64((abs(numerator) // abs(denominator)) *
               (-1 if (numerator < 0) != (denominator < 0) else 1))


def rounded_root(squared):
    return s32(math.isqrt((squared << 2) & MASK64) + 1) >> 1


def normalize(vector, narrow=False, clamp=False):
    squared = sum(value * value for value in vector)
    squared = s32(squared) if narrow else s64(squared)
    reciprocal = quotient(1 << 56, squared)
    root = s32(math.isqrt((squared << 2) & MASK64))
    scale = (reciprocal * root) & MASK64
    result = [s64(scale * value + (1 << 44)) >> 45 for value in vector]
    if narrow:
        result = [s16(value) for value in result]
        if clamp:
            result = [min(value, 4095) for value in result]
    return tuple(result)


class Cpu:
    def __init__(self):
        self.cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        self.pages = set()
        self.failed = None
        for address, size, perms in ((BUFFER, 0x10000, 3),
                                     (STACK, 0x10000, 3), (STOP, 4096, 5)):
            self.map(address, size, perms)

    def map(self, address, size, perms=7):
        for page in range(address & ~4095, (address + size + 4095) & ~4095, 4096):
            if page not in self.pages:
                self.cpu.mem_map(page, 4096, perms)
                self.pages.add(page)

    def write(self, address, data):
        self.cpu.mem_write(address, data)

    def read(self, address, size):
        return bytes(self.cpu.mem_read(address, size))

    def invoke(self, address, arguments, expect_failure=False):
        self.failed = None
        self.cpu.reg_write(UC_ARM_REG_CPSR, 0x10)
        for i, reg in enumerate(ARGS):
            self.cpu.reg_write(reg, (arguments[i] if i < len(arguments) else 0) & MASK32)
        sp = STACK + 0x8000
        if len(arguments) > 4:
            self.write(sp, struct.pack('<' + 'I' * (len(arguments) - 4),
                                       *(value & MASK32 for value in arguments[4:])))
        self.cpu.reg_write(UC_ARM_REG_SP, sp)
        self.cpu.reg_write(UC_ARM_REG_LR, STOP)
        for i, reg in enumerate(SAVED):
            self.cpu.reg_write(reg, 0x12340000 + i)
        self.cpu.emu_start(address, STOP, count=200000)
        if expect_failure:
            assert self.failed, 'Native invalid input did not fail explicitly'
        else:
            assert self.failed is None, self.failed
            assert self.cpu.reg_read(UC_ARM_REG_PC) == STOP
            assert self.cpu.reg_read(UC_ARM_REG_SP) == sp
            assert all(self.cpu.reg_read(reg) == 0x12340000 + i
                       for i, reg in enumerate(SAVED)), 'Callee-saved ARM registers changed'
        return (self.cpu.reg_read(UC_ARM_REG_R0), self.cpu.reg_read(UC_ARM_REG_R1))


native = Cpu()
elf = elf_path.read_bytes()
phoff = struct.unpack_from('<I', elf, 28)[0]
phsize, phnum = struct.unpack_from('<HH', elf, 42)
for i in range(phnum):
    kind, off, va, pa, filesz, memsz, perms, align = struct.unpack_from('<8I', elf, phoff + i * phsize)
    if kind == 1 and memsz:
        native.map(va, memsz)
        native.write(va, elf[off:off + filesz])


def failed_native(uc, address, size, user):
    pointer = uc.reg_read(UC_ARM_REG_R0)
    native.failed = native.read(pointer, 200).split(b'\0', 1)[0].decode()
    uc.emu_stop()


native.cpu.hook_add(UC_HOOK_CODE, failed_native,
                   begin=symbols['sm64ds_native_missing'], end=symbols['sm64ds_native_missing'])
original = Cpu()
raw = (ROOT / 'build/roms/europe.nds').read_bytes()
assert zlib.crc32(raw) == 0x29715dec, 'Requires verified ASMP0 source ROM'
rom = ndspy.rom.NintendoDSRom(raw)
arm9 = ndspy.codeCompression.decompress(rom.arm9)
overlay = rom.loadArm9Overlays([7])[7]
original_ranges = ((0x020c30ac, 0x134), (0x020c31e0, 0x108),
                   (0x020c368c, 0x36c), (0x020c7d60, 0x2b0))
for address, size in original_ranges:
    original.map(address, size, 5)
    offset = address - overlay.ramAddress
    original.write(address, bytes(overlay.data[offset:offset + size]))
for address, size in ((0x02053320, 0x60), (0x0205380c, 0x44), (0x02053850, 0x34)):
    original.map(address, size, 5)
    offset = address - rom.arm9RamAddress
    original.write(address, bytes(arm9[offset:offset + size]))
original.map(0x04000000, 4096, 3)


def original_arithmetic(uc, access, address, size, value, user):
    if address in (0x040002a0, 0x040002a4):
        numerator, denominator = struct.unpack('<qq', uc.mem_read(0x04000290, 16))
        uc.mem_write(0x040002a0, struct.pack('<Q', quotient(numerator, denominator) & MASK64))
    elif address == 0x040002b4:
        radicand = struct.unpack('<Q', uc.mem_read(0x040002b8, 8))[0]
        mode = struct.unpack('<H', uc.mem_read(0x040002b0, 2))[0]
        if not mode & 1:
            radicand &= MASK32
        uc.mem_write(address, struct.pack('<I', math.isqrt(radicand)))


original.cpu.hook_add(UC_HOOK_MEM_READ, original_arithmetic,
                     begin=0x040002a0, end=0x040002b7)
assert 0x04000000 not in native.pages
cpus = (native, original)
counts = {name: 0 for name in NAMES}
rng = random.Random(0x020c368c)


def nearest3(points, target, radius, write_distance=True):
    image = bytearray(CANARY * 1024)
    struct.pack_into('<' + 'i' * (3 * len(points)), image, 8, *(v for p in points for v in p))
    struct.pack_into('<3i', image, 512, *target)
    expected = bytearray(image)
    squared = [s64(sum(s32(a - b) ** 2 for a, b in zip(p, target))) for p in points]
    best = min(range(len(points)), key=lambda i: squared[i])
    accepted = radius < 0 or radius * radius >= squared[best]
    if accepted and write_distance:
        struct.pack_into('<i', expected, 768, rounded_root(squared[best]))
    result = best if accepted else -1
    for cpu, address in zip(cpus, (symbols[NAMES[0]], original_ranges[0][0])):
        cpu.write(BUFFER, bytes(image))
        actual, _ = cpu.invoke(address, (BUFFER + 8, len(points), BUFFER + 512,
                                         radius, BUFFER + 768 if write_distance else 0))
        assert s32(actual) == result, ('nearest3', points, target, radius, s32(actual), result)
        assert cpu.read(BUFFER, len(image)) == expected
    counts[NAMES[0]] += 1


edge32 = (0, 1, -1, 4096, -4096, -(1 << 31), (1 << 31) - 1)
for point in itertools.product(edge32, repeat=3):
    nearest3((point, (0, 0, 0)), (17, -19, 23), -1)
for radius in (-1, 0, 4, 5, 4096, (1 << 31) - 1):
    nearest3(((3, 4, 0), (3, 4, 0)), (0, 0, 0), radius)
    nearest3(((3, 4, 0),), (0, 0, 0), radius, False)
for i in range(800):
    limit = (1 << 31) if i % 2 else 100000
    points = tuple(tuple(rng.randrange(-limit, limit) for _ in range(3))
                   for _ in range((1, 2, 7)[i % 3]))
    target = tuple(rng.randrange(-limit, limit) for _ in range(3))
    nearest3(points, target, rng.choice((-1, 0, 4096, (1 << 31) - 1)), i % 3 != 0)


def nearest2(points, center, radius, write_distance=True, count=None):
    count = len(points) if count is None else count
    image = bytearray(CANARY * 1024)
    if points:
        struct.pack_into('<' + 'i' * len(points), image, 8, *(p[0] for p in points))
        struct.pack_into('<' + 'i' * len(points), image, 256, *(p[1] for p in points))
    expected = bytearray(image)
    best, best_squared = -1, (1 << 63) - 1
    for i in range(count):
        squared = s64(sum(s32(a - b) ** 2 for a, b in zip(center, points[i])))
        if squared < best_squared:
            best, best_squared = i, squared
    accepted = radius < 0 or radius * radius >= best_squared
    if accepted and write_distance:
        struct.pack_into('<i', expected, 768, rounded_root(best_squared))
    for cpu, address in zip(cpus, (symbols[NAMES[1]], original_ranges[1][0])):
        cpu.write(BUFFER, bytes(image))
        actual, _ = cpu.invoke(address, (BUFFER + 8, BUFFER + 256, count,
                                         *center, radius, BUFFER + 768 if write_distance else 0))
        assert s32(actual) == (best if accepted else -1)
        assert cpu.read(BUFFER, len(image)) == expected
    counts[NAMES[1]] += 1


for point in itertools.product(edge32, repeat=2):
    nearest2((point, (0, 0)), (17, -19), -1)
for count in (0, -1):
    for radius in (-1, 0):
        nearest2((), (0, 0), radius, count=count)
for radius in (-1, 0, 4, 5, 4096, (1 << 31) - 1):
    nearest2(((3, 4), (3, 4)), (0, 0), radius)
    nearest2(((3, 4),), (0, 0), radius, False)
for i in range(800):
    limit = (1 << 31) if i % 2 else 100000
    points = tuple(tuple(rng.randrange(-limit, limit) for _ in range(2))
                   for _ in range((1, 2, 7)[i % 3]))
    center = tuple(rng.randrange(-limit, limit) for _ in range(2))
    nearest2(points, center, rng.choice((-1, 0, 4096, (1 << 31) - 1)), i % 3 != 0)


def normal16(vectors, clamp, count=None):
    count = len(vectors) if count is None else count
    image = bytearray(CANARY * (12 + len(vectors) * 6))
    expected = bytearray(image)
    for i, vector in enumerate(vectors):
        struct.pack_into('<3h', image, 6 + i * 6, *vector)
        result = normalize(vector, True, clamp) if i < max(1, count) else vector
        struct.pack_into('<3h', expected, 6 + i * 6, *result)
    for cpu, address in zip(cpus, (symbols[NAMES[2]], original_ranges[2][0])):
        cpu.write(BUFFER, bytes(image))
        cpu.invoke(address, (BUFFER + 6, count, clamp))
        assert cpu.read(BUFFER, len(image)) == expected, ('normal16', vectors, count, clamp)
    counts[NAMES[2]] += 1


edge16 = (0, 1, -1, 4096, -4096, -32768, 32767)
for vector in itertools.product(edge16, repeat=3):
    for clamp in (0, 1):
        normal16((vector,), clamp)
for i in range(1400):
    limit = 32768 if i % 2 else 4096
    vector = tuple(rng.randrange(-limit, limit) for _ in range(3))
    normal16((vector,), i % 2)
for count in (0, -3):
    normal16(((4096, 0, 0), (3000, 3000, 0)), 1, count)
for count in (2, 3, 33):
    vectors = tuple(tuple(rng.randrange(-32768, 32768) for _ in range(3)) for _ in range(count))
    for clamp in (0, 1, -1):
        normal16(vectors, clamp)


def q12(a, b, rounding=0):
    return s32(s64(a * b + rounding) >> 12)


def spring(position1, position2, velocity1, velocity2, force1, force2,
           rest, stiffness, friction, same_particle=False):
    image = bytearray(CANARY * 0x300)
    first, second, link = BUFFER + 0x100, BUFFER + 0x140, BUFFER + 0x280
    if same_particle:
        second = first
    struct.pack_into('<I6i', image, 0x100, BUFFER + 0x200, *velocity1, *force1)
    if not same_particle:
        struct.pack_into('<I6i', image, 0x140, BUFFER + 0x220, *velocity2, *force2)
    struct.pack_into('<3i', image, 0x200, *position1)
    struct.pack_into('<3i', image, 0x220, *position2)
    struct.pack_into('<2Ii', image, 0x280, first, second, rest)
    expected = bytearray(image)
    direction = tuple(s32(a - b) for a, b in zip(position1,
                         position1 if same_particle else position2))
    squared = sum(v * v for v in direction) & MASK64
    length_bits = s32(math.isqrt((squared << 2) & MASK64) + 1)
    length = (abs(length_bits) // 2) * (-1 if length_bits < 0 else 1)
    restoring = s32(-q12(s32(length - rest), stiffness, 0x800))
    direction = normalize(direction)
    result1 = tuple(s32(b + q12(restoring, a)) for a, b in zip(direction, force1))
    result2 = tuple(s32(b + q12(s32(-restoring), a)) for a, b in zip(direction,
                               result1 if same_particle else force2))
    dot1 = s32(s64(sum(a * b for a, b in zip(direction, velocity1)) + 0x800) >> 12)
    dot2 = s32(s64(sum(a * b for a, b in zip(direction,
                         velocity1 if same_particle else velocity2)) + 0x800) >> 12)
    projected1 = tuple(q12(dot1, a) for a in direction)
    projected2 = tuple(q12(dot2, a) for a in direction)
    drag = s32(-friction)
    result1 = tuple(s32(b + q12(drag, a)) for a, b in zip(projected1,
                               result2 if same_particle else result1))
    result2 = tuple(s32(b + q12(drag, a)) for a, b in zip(projected2,
                               result1 if same_particle else result2))
    struct.pack_into('<3i', expected, 0x110, *(result2 if same_particle else result1))
    if not same_particle:
        struct.pack_into('<3i', expected, 0x150, *result2)
    for cpu, address in zip(cpus, (symbols[NAMES[3]], original_ranges[3][0])):
        cpu.write(BUFFER, bytes(image))
        cpu.invoke(address, (link, stiffness, friction))
        assert cpu.read(BUFFER, len(image)) == expected, ('spring', direction, rest, stiffness, friction)
    counts[NAMES[3]] += 1


for position in itertools.product(edge32, repeat=3):
    spring(position, (0, 0, 0), ((1 << 31) - 1, 0, -(1 << 31)),
           (0, -17, 19), (17, -19, (1 << 31) - 1), (23, -29, 31), 4096, 4096, 1024)
for i in range(1000):
    limit = (1 << 31) if i % 2 else 100000
    vectors = [tuple(rng.randrange(-limit, limit) for _ in range(3)) for _ in range(6)]
    spring(*vectors, *(rng.choice(edge32) for _ in range(3)), same_particle=i % 11 == 0)

# Snapshot tests intentionally have only a native oracle: the format stores
# software state, not a byte reconstruction of the original DS register dump.
async_name = '_ZN4cstd10fdiv_asyncE5Fix12IiE5Fix12IiE'
long_name = '_ZN4cstd11ldiv_resultEv'
fixed_name = '_ZN4cstd11fdiv_resultEv'
snapshot_cases = list(itertools.product(edge32, repeat=2))
snapshot_cases += [(rng.randrange(-(1 << 31), 1 << 31), rng.randrange(-(1 << 31), 1 << 31))
                   for _ in range(800)]
for numerator, denominator in snapshot_cases:
    expected_q = quotient(numerator * (1 << 32), denominator)
    native.write(BUFFER, CANARY * 80)
    native.invoke(symbols[async_name], (numerator, denominator))
    native.invoke(symbols['ARMMathSaveState'], (BUFFER + 8,))
    snapshot = native.read(BUFFER, 80)
    assert snapshot[:8] == CANARY * 8 and snapshot[36:] == CANARY * 44
    native.invoke(symbols[async_name], (17, 19))
    native.invoke(symbols['ARMMathLoadState'], (BUFFER + 8,))
    lo, hi = native.invoke(symbols[long_name], ())
    assert s64((hi << 32) | lo) == expected_q
    actual, _ = native.invoke(symbols[fixed_name], ())
    expected_fixed = s32(s64(expected_q + 0x80000) >> 20)
    assert s32(actual) == expected_fixed
    assert native.read(BUFFER, 80) == snapshot

# Nested snapshots restore the independently saved earlier quotient.
native.invoke(symbols[async_name], (-1, 6))
native.invoke(symbols['ARMMathSaveState'], (BUFFER + 8,))
native.invoke(symbols[async_name], (1, 6))
native.invoke(symbols['ARMMathSaveState'], (BUFFER + 40,))
native.invoke(symbols[async_name], (3, 7))
native.invoke(symbols['ARMMathLoadState'], (BUFFER + 40,))
assert s32(native.invoke(symbols[fixed_name], ())[0]) == 683
native.invoke(symbols['ARMMathLoadState'], (BUFFER + 8,))
assert s32(native.invoke(symbols[fixed_name], ())[0]) == -683
snapshot = native.read(BUFFER + 8, 28)
for word in range(7):
    corrupt = bytearray(snapshot)
    corrupt[word * 4] ^= 1
    native.write(BUFFER + 8, bytes(corrupt))
    native.invoke(symbols['ARMMathLoadState'], (BUFFER + 8,), expect_failure=True)
for name in ('ARMMathSaveState', 'ARMMathLoadState'):
    native.invoke(symbols[name], (0,), expect_failure=True)
for count in (0, -1):
    native.invoke(symbols[NAMES[0]], (BUFFER + 8, count, BUFFER + 512, -1, BUFFER + 768),
                  expect_failure=True)

report = {
    'status': 'passed',
    'scope': 'Native ARM11 CPU leaves vs supplied-ROM leaves and independent Python integers; no gameplay, 3DS or FPS proof',
    'native_DS_IO_mapping': False,
    'original_oracle': 'Only bounded leaf byte ranges; original DS arithmetic reads modeled',
    'upstream_pin': '6230b2996636e3fd0e03cb178161c94cb4b73cc9',
    'comparisons_per_leaf': counts,
    'total_leaf_comparisons': sum(counts.values()),
    'snapshot_roundtrips': len(snapshot_cases),
    'nested_snapshots': 'passed',
    'explicit_invalid_input_checks': 11,
    'canaries_and_ARM_calling_convention': 'passed',
    'ROM_sha256': hashlib.sha256(raw).hexdigest(),
    'native_ELF_sha256': hashlib.sha256(elf).hexdigest(),
    'source_sha256': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in sources},
    'object_sha256': {str(obj.relative_to(ROOT)): hashlib.sha256(obj.read_bytes()).hexdigest() for obj in objects},
}
(OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
