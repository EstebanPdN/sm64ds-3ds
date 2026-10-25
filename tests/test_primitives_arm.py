#!/usr/bin/env python3
"""Run the actual ARM11 primitive machine code against memory/ABI contracts.

Requires devkitARM and the test-only Python dependency unicorn==2.1.4.
This emulates CPU instructions, not a 3DS system or game performance.
"""
import argparse
import hashlib
import json
import pathlib
import struct
import subprocess
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_MEM_WRITE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8,
    UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_SP, UC_ARM_REG_LR,
    UC_ARM_REG_CPSR, UC_ARM_REG_PC)

CODE, MEMORY, STACK, STOP = 0x100000, 0x200000, 0x300000, 0x400000
SOURCE, DEST = MEMORY + 0x100, MEMORY + 0x10100
SAVED = [UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
         UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain', type=pathlib.Path, default=pathlib.Path('/opt/devkitpro/devkitARM/bin'))
    parser.add_argument('--report', type=pathlib.Path)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    tool = lambda name: str(args.toolchain/f'arm-none-eabi-{name}')
    with tempfile.TemporaryDirectory(prefix='sm64ds-primitives-') as temp:
        work = pathlib.Path(temp)
        flags = ['-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft', '-Os',
                 '-ffunction-sections', '-fdata-sections', '-Wa,--noexecstack']
        subprocess.run([tool('gcc'), *flags, '-c', str(root/'source/ctr/game_memory_primitives.S'), '-o', str(work/'asm.o')], check=True)
        subprocess.run([tool('g++'), *flags, '-std=c++17', '-I'+str(root/'include'), '-c', str(root/'source/ctr/game_primitives.cpp'), '-o', str(work/'matrix.o')], check=True)
        # Keep this regression scoped to its original three tested leaves.
        # Other matrix constructors now share matrix.o and may use libc;
        # retaining them would create an unrelated unresolved memset link.
        subprocess.run([tool('ld'), '-Ttext='+hex(CODE), '-e', 'CpuCopy8',
                        '--gc-sections', '-z', 'noexecstack',
                        '-u', 'DecompressLZ16', '-u', 'Matrix3x3_SetRotationZ',
                        str(work/'asm.o'), str(work/'matrix.o'),
                        '-o', str(work/'test.elf')], check=True)
        subprocess.run([tool('objcopy'), '-O', 'binary', str(work/'test.elf'), str(work/'test.bin')], check=True)
        symbols = {}
        for line in subprocess.check_output([tool('nm'), '-g', '--defined-only', str(work/'test.elf')], text=True).splitlines():
            address, kind, name = line.split()
            if kind in 'Tt':
                symbols[name] = int(address, 16)
        binary = (work/'test.bin').read_bytes()
    cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    cpu.mem_map(CODE, 0x10000); cpu.mem_write(CODE, binary)
    cpu.mem_map(MEMORY, 0x20000)
    cpu.mem_map(STACK, 0x10000)
    cpu.mem_map(STOP, 0x1000)
    writes = []
    cpu.hook_add(UC_HOOK_MEM_WRITE, lambda uc, access, address, size, value, user: writes.append((address, size)))

    def call(symbol, first, second, third=0):
        writes.clear()
        cpu.reg_write(UC_ARM_REG_CPSR, 0x10)  # ARM user mode, no privileged environment.
        cpu.reg_write(UC_ARM_REG_R0, first); cpu.reg_write(UC_ARM_REG_R1, second)
        cpu.reg_write(UC_ARM_REG_R2, third)
        cpu.reg_write(UC_ARM_REG_SP, STACK+0x8000); cpu.reg_write(UC_ARM_REG_LR, STOP)
        for i, reg in enumerate(SAVED):
            cpu.reg_write(reg, 0x87650000+i)
        cpu.emu_start(symbols[symbol], STOP, count=1000000)
        assert cpu.reg_read(UC_ARM_REG_PC) == STOP, 'Function did not return'
        assert cpu.reg_read(UC_ARM_REG_SP) == STACK+0x8000, 'Stack not restored'
        assert all(cpu.reg_read(reg) == 0x87650000+i for i, reg in enumerate(SAVED)), 'Callee-saved register clobbered'
        return [(a, n) for a, n in writes if MEMORY <= a < MEMORY+0x20000]

    copy_cases = 0
    source = bytes((i*73 + 19) & 255 for i in range(1024))
    for source_offset in range(4):
        for dest_offset in range(4):
            for size in [*range(40), 63, 64, 65, 127, 128, 129, 255]:
                cpu.mem_write(SOURCE, source)
                cpu.mem_write(DEST-4, bytes([0xa5])*512)
                moves = call('CpuCopy8', SOURCE+source_offset, DEST+dest_offset, size)
                expected = bytearray([0xa5]*512)
                expected[4+dest_offset:4+dest_offset+size] = source[source_offset:source_offset+size]
                assert cpu.mem_read(DEST-4, 512) == expected, (source_offset, dest_offset, size)
                assert all(n in (2, 4) for _, n in moves), 'Unexpected byte store in CpuCopy8'
                assert not moves if size == 0 else moves
                copy_cases += 1
    cpu.mem_write(SOURCE, source)
    call('CpuCopy8', SOURCE+1, SOURCE+1, 129)
    assert cpu.mem_read(SOURCE, len(source)) == source
    copy_cases += 1

    def compressed(tokens, declared_size=None):
        body = bytearray(); expected = bytearray()
        for start in range(0, len(tokens), 8):
            group = tokens[start:start+8]
            body.append(sum((1 << (7-i)) for i, token in enumerate(group) if isinstance(token, tuple)))
            for token in group:
                if isinstance(token, tuple):
                    length, distance = token
                    assert 3 <= length <= 18 and 1 <= distance <= 4096 and distance <= len(expected)
                    body.extend((((length-3)<<4) | ((distance-1)>>8), (distance-1)&255))
                    for _ in range(length): expected.append(expected[-distance])
                else:
                    body.append(token); expected.append(token)
        size = len(expected) if declared_size is None else declared_size
        return struct.pack('<I', (size<<8)|0x10)+body, expected

    fixtures = [([], None), ([10, 20], None), (list(range(17)), None),
                ([65, 66, (18, 2), (12, 2)], None),
                ([65, 66, 67, (18, 3), (9, 3)], None),
                ([*range(7), (18, 7), (18, 7)], None),
                ([i&255 for i in range(4096)]+[(18, 4096)], None),
                # Characterize original token-overrun behavior, not permission
                # for the resource loader to accept a malformed compressed file.
                ([65, 66, (18, 2)], 6)]
    for tokens, declared in fixtures:
        packed, expected = compressed(tokens, declared)
        cpu.mem_write(SOURCE, packed)
        cpu.mem_write(DEST-4, bytes([0xa5])*(len(expected)+16))
        moves = call('DecompressLZ16', SOURCE, DEST)
        written = len(expected) & ~1
        want = bytes([0xa5])*4 + expected[:written] + bytes([0xa5])*(len(expected)+12-written)
        assert cpu.mem_read(DEST-4, len(want)) == want, (len(expected), declared)
        assert all(n == 2 for _, n in moves), 'Unexpected store width in DecompressLZ16'

    matrix_cases = 0
    for sine in (0, 4096, -4096, 2896, -1337, -0x80000000, 0x7fffffff):
        for cosine in (0, 4096, -4096, 2896, -1337, -0x80000000, 0x7fffffff):
            cpu.mem_write(DEST-4, bytes([0xa5])*44)
            call('Matrix3x3_SetRotationZ', DEST, sine & 0xffffffff, cosine & 0xffffffff)
            values = [cosine, sine, 0, -sine, cosine, 0, 0, 0, 4096]
            expected = bytes([0xa5])*4 + struct.pack('<9I', *(v & 0xffffffff for v in values)) + bytes([0xa5])*4
            assert cpu.mem_read(DEST-4, 44) == expected
            matrix_cases += 1
    result = {'status': 'passed', 'cpu': 'ARM user-mode instructions, not physical 3DS',
              'binary_sha256': hashlib.sha256(binary).hexdigest(), 'symbols': sorted(symbols),
              'copy_cases': copy_cases, 'lz16_cases': len(fixtures), 'matrix_cases': matrix_cases,
              'callee_saved_registers_preserved': True, 'halfword_store_contract_preserved': True}
    text = json.dumps(result, indent=2)+'\n'
    if args.report: args.report.write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
