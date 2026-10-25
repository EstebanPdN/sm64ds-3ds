#!/usr/bin/env python3
"""Check menu matrix constructors against source-ROM bytes and CPU behavior.

The native sources compile for ARMv6K. Only the six original pure-memory leaves
are copied from the supplied ROM into the CPU oracle. No DS MMIO, scene, Azahar,
3DS system, gameplay or frame timing is part of this test.
"""
import hashlib
import itertools
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
import zlib

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
    UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR)

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/native-identities'
TOOL = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
OUT.mkdir(exist_ok=True)
sys.path.insert(0, str(ROOT / 'build/game-data/python'))
import ndspy.rom
import ndspy.codeCompression

# name, ROM address, instruction bytes, output words, execution mode.
LEAVES = (
    ('Matrix3x3_LoadIdentity', 0x020524f0, 0x24, 9, False),
    ('Matrix4x3_LoadIdentity', 0x020527c0, 0x28, 12, False),
    ('func_02052ec8', 0x02052ec8, 0x2c, 16, False),
    ('func_02052550', 0x02052550, 0x1c, 9, True),
    ('func_0205256c', 0x0205256c, 0x1c, 9, True),
    ('func_020527e8', 0x020527e8, 0x16, 12, True),
)
sources = [ROOT / 'source/ctr/game_memory_primitives.S']
sources.extend(ROOT / 'build/native-core/generated/src' / (name + '.c')
    for name in ('Matrix3x3_LoadIdentity', 'Matrix4x3_LoadIdentity'))
objects = []
for source in sources:
    if not source.is_file():
        raise RuntimeError('Run tools/native_core.py to generate existing identity assembly: ' + str(source))
    obj = OUT / (source.stem + '.o')
    subprocess.run([TOOL + 'gcc', '-march=armv6k', '-mtune=mpcore',
        '-mfloat-abi=hard', '-mtp=soft', '-x', 'assembler-with-cpp',
        '-Wa,--noexecstack', '-c', str(source), '-o', str(obj)], check=True)
    objects.append(obj)
elf_path = OUT / 'identities.elf'
subprocess.run([TOOL + 'ld', '-Ttext=0x100000', '-e', LEAVES[2][0],
    '-z', 'noexecstack', *map(str, objects), '-o', str(elf_path)], check=True)
symbols = {fields[2]: int(fields[0], 16) for line in subprocess.check_output(
    [TOOL + 'nm', '--defined-only', str(elf_path)], text=True).splitlines()
    if len(fields := line.split()) == 3}

cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
elf = elf_path.read_bytes()
phoff = struct.unpack_from('<I', elf, 28)[0]
phsize, phnum = struct.unpack_from('<HH', elf, 42)
pages = set()
for i in range(phnum):
    typ, off, va, _, filesz, memsz, _, _ = struct.unpack_from('<8I', elf, phoff + i * phsize)
    if typ != 1 or not memsz:
        continue
    for page in range(va & ~4095, (va + memsz + 4095) & ~4095, 4096):
        if page not in pages:
            cpu.mem_map(page, 4096, 5)
            pages.add(page)
    cpu.mem_write(va, elf[off:off + filesz])

raw = (ROOT / 'build/roms/europe.nds').read_bytes()
assert zlib.crc32(raw) == 0x29715dec, 'Requires verified ASMP0 source ROM'
rom = ndspy.rom.NintendoDSRom(raw)
arm9 = ndspy.codeCompression.decompress(rom.arm9)
cpu.mem_map(0x02052000, 4096, 5)
code_hashes = {}
for name, original, size, _, _ in LEAVES:
    original_bytes = bytes(arm9[original - rom.arm9RamAddress:
                                original - rom.arm9RamAddress + size])
    cpu.mem_write(original, original_bytes)
    native_bytes = bytes(cpu.mem_read(symbols[name] & ~1, size))
    assert native_bytes == original_bytes, ('Machine bytes differ', name,
                                            native_bytes.hex(), original_bytes.hex())
    code_hashes[name] = hashlib.sha256(native_bytes).hexdigest()

BUFFER, STACK, STOP = 0x500000, 0x600000, 0x700000
cpu.mem_map(BUFFER, 8192, 3)
cpu.mem_map(STACK, 4096, 3)
cpu.mem_map(STOP, 4096, 5)
args = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)
saved = (UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
         UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11)
observable = (*args, UC_ARM_REG_R12)
writes, reads = [], []
cpu.hook_add(UC_HOOK_MEM_WRITE,
    lambda _, access, address, size, value, user: writes.append((address, size)))
cpu.hook_add(UC_HOOK_MEM_READ,
    lambda _, access, address, size, value, user: reads.append((address, size)))
rng = random.Random(0x52ec8)


def invoke(address, thumb, destination, scalars, initial, nwords, nzcv):
    cpu.mem_write(destination - 32, initial)
    cpu.reg_write(UC_ARM_REG_CPSR, 0x10 | nzcv)
    for reg, value in zip(args, (destination, *scalars)):
        cpu.reg_write(reg, value & 0xffffffff)
    cpu.reg_write(UC_ARM_REG_R12, 0x197ac35e)
    cpu.reg_write(UC_ARM_REG_SP, STACK + 0xf00)
    cpu.reg_write(UC_ARM_REG_LR, STOP)
    for i, reg in enumerate(saved):
        cpu.reg_write(reg, 0x12345670 + i)
    writes.clear()
    reads.clear()
    cpu.emu_start((address & ~1) | int(thumb), STOP, count=1000)
    assert cpu.reg_read(UC_ARM_REG_PC) == STOP, 'No return'
    assert cpu.reg_read(UC_ARM_REG_SP) == STACK + 0xf00, 'SP changed'
    assert all(cpu.reg_read(reg) == 0x12345670 + i for i, reg in enumerate(saved)), 'ABI clobber'
    assert not reads, ('Constructor unexpectedly reads memory', reads)
    assert len(writes) == nwords
    assert sorted(writes) == [(destination + 4 * i, 4) for i in range(nwords)], 'Write extent/order width'
    contents = bytes(cpu.mem_read(destination - 32, len(initial)))
    assert contents[:32] == initial[:32] and contents[32 + nwords * 4:] == initial[32 + nwords * 4:], 'Canary overwritten'
    registers = tuple(cpu.reg_read(reg) for reg in observable)
    flags = cpu.reg_read(UC_ARM_REG_CPSR) & 0xf0000000
    return contents[32:32 + nwords * 4], registers, flags, tuple(writes)


def expected(name, values):
    a, b, c = values
    if name == 'Matrix3x3_LoadIdentity':
        return (4096, 0, 0, 0, 4096, 0, 0, 0, 4096)
    if name == 'Matrix4x3_LoadIdentity':
        return (4096, 0, 0, 0, 4096, 0, 0, 0, 4096, 0, 0, 0)
    if name == 'func_02052ec8':
        return tuple(4096 if i in (0, 5, 10, 15) else 0 for i in range(16))
    if name == 'func_02052550':
        return (4096, 0, 0, 0, b, a, 0, -a, b)
    if name == 'func_0205256c':
        return (b, 0, -a, 0, 4096, 0, a, 0, b)
    if name == 'func_020527e8':
        return (a, 0, 0, 0, b, 0, 0, 0, c, 0, 0, 0)
    raise AssertionError(name)


edges = (0, 1, 0xffffffff, 4096, 0xfffff000, 2896, 0x80000000, 0x7fffffff)
counts = {}
for name, original, _, nwords, thumb in LEAVES:
    if name in ('func_02052550', 'func_0205256c'):
        cases = [(a, b, 0x7bada55) for a, b in itertools.product(edges, repeat=2)]
    elif name == 'func_020527e8':
        cases = list(itertools.product(edges, repeat=3))
    else:
        cases = [(rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32)) for _ in range(128)]
    cases.extend(tuple(rng.getrandbits(32) for _ in range(3)) for _ in range(1000))
    for i, values in enumerate(cases):
        # Includes destinations crossing a mapped page boundary, all word aligned.
        offsets = (64, 68, 72, 76, 4096 - 32, 4096 - nwords * 4)
        destination = BUFFER + offsets[i % len(offsets)]
        initial = rng.randbytes(64 + 4 * nwords)
        nzcv = (i % 16) << 28
        native = invoke(symbols[name], thumb, destination, values, initial, nwords, nzcv)
        oracle = invoke(original, thumb, destination, values, initial, nwords, nzcv)
        assert native == oracle, (name, values, native, oracle)
        want = struct.pack('<' + str(nwords) + 'I', *(v & 0xffffffff for v in expected(name, values)))
        assert native[0] == want, (name, values, native[0].hex(), want.hex())
    counts[name] = len(cases)

report = {'status': 'passed',
    'scope': 'Unicorn pure-memory CPU leaves only; no scene, emulator application, physical console or FPS proof',
    'rom_sha256': hashlib.sha256(raw).hexdigest(),
    'native_sources_sha256': {str(source.relative_to(ROOT)): hashlib.sha256(source.read_bytes()).hexdigest()
                            for source in sources},
    'native_elf_sha256': hashlib.sha256(elf).hexdigest(),
    'machine_bytes_identical_to_rom': code_hashes,
    'comparisons': counts, 'total_comparisons': sum(counts.values()),
    'canaries': 'passed; exact stores only inside each matrix',
    'ABI': 'passed; SP/r4-r11 preserved; r0-r3/ip/NZCV and store order equal to ROM'}
(OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
