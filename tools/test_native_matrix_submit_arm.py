#!/usr/bin/env python3
"""Compare native camera matrices/ordered GX submissions against the source ROM.

Executes the ARM11 native provider with DS I/O unmapped. A separate bounded
ARM9 oracle models only divider/sqrt registers and records geometry writes.
The host test feeds the real native stream to the real GX decoder. These are
CPU/ABI/decoder checks, not game, Azahar, physical-console or FPS evidence.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import zlib

from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
    UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR)

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'build/game-data/python'))
import ndspy.rom
import ndspy.code
import ndspy.codeCompression
import native_scene_video

PIN = '6230b2996636e3fd0e03cb178161c94cb4b73cc9'
TOOL = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
LOOK = '_ZN3G3i7LookAt_EPK7Vector3S2_S2_bP9Matrix4x3'
PERSPECTIVE = '_ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3'
CAMERA = 'func_ov007_020c0078'
BUFFER, STACK, STOP = 0x500000, 0x600000, 0x700000
MASK32, MASK64 = (1 << 32)-1, (1 << 64)-1
ARGS = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)
SAVED = (UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
         UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11)
REVIEWED = {
    '_ZN3G3i7LookAt_EPK7Vector3S2_S2_bP9Matrix4x3.cpp': '2aa94544cfcd61e119286ff90eef676f9efb677382c555abf3748812532e1d2f',
    'func_ov007_020b1fa4.c': '5bd92487967554ae1bd2fa2738a902ba4058cc45639f205142465645f95a5b51',
    'func_ov007_020b20e8.c': 'b831888d9f01c73c33cba72d65796cf773a8646bb3f9b6c9bc28e340ecdd9908',
    'func_ov007_020b2160.c': 'bb67c17103cddd22ae633276a3c7666293284e41ad812568951e58722297cc2e',
    'func_ov007_020c0078.c': 'c5bcdd5dcd32e2e414181600ee5e1e51be88e0d2b34eccc60b93cf9830ddacf6',
    'func_ov007_020bffb8.c': 'c9487d3d92ebdffa52df3d46f7c637a50ba744453f038b9fd45f5483d86aac6e',
    'func_ov007_020bff38.c': '2a9d52505ed344a23059ab39a9551f39d4c8a2ca91d4e799b83af16d3eb55c6f',
    'func_ov007_020c0128.cpp': '1f1c9bf34fd984b116f1df7ef372b4542b64358982f703ac5185cae7c1155461',
    '_ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3.c': '0d03108c356e98159f1936e2bf44ae20d8fb72e684000ac2db442c2bf55878b8',
    'NormalizeVec3.c': 'c02bf9420d01f0ebe8b5b92ec9abd46c534e6dfaad66662098fc1e10b55a26d5',
    'CrossVec3.c': '010607d6959095ccd7ae6cda3992ac0a314d5111b3ff939670736dfa749a8f0e',
    'DotVec3.c': '60e61f461e5bbbaa476a437893ee12f74697ad302cb1e5509093485b2a870ece',
    'func_02055998.c': '1f71534f5fa4c727bcfb66162fa5651cc7886e4b93ab7e0e9aac0496e9cc5634',
}
NATIVE = ['source/game_port/native_matrix_submit.cpp', 'source/game_port/native_projection.cpp',
          'source/ctr/game_math.cpp', 'source/core/fixed_math.cpp',
          'source/core/game_draw.cpp', 'source/game_port/native_video.cpp',
          'tests/native_matrix_submit_boundary.cpp', 'upstream/src/CrossVec3.c',
          'upstream/src/DotVec3.c']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def signed(value, width=32):
    value &= (1 << width)-1
    return value-(1 << width) if value & (1 << (width-1)) else value


class Cpu:
    def __init__(self):
        self.cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        self.pages = set()
        self.failed = None
        self.map(BUFFER, 0x10000, 3)
        self.map(STACK, 0x10000, 3)
        self.map(STOP, 4096, 5)

    def map(self, address, size, perms=7):
        for page in range(address & ~4095, (address+size+4095) & ~4095, 4096):
            if page not in self.pages:
                self.cpu.mem_map(page, 4096, perms)
                self.pages.add(page)

    def write(self, address, data):
        self.cpu.mem_write(address, bytes(data))

    def read(self, address, size):
        return bytes(self.cpu.mem_read(address, size))

    def invoke(self, address, arguments=(), fail=False, instructions=30000):
        self.failed = None
        self.cpu.reg_write(UC_ARM_REG_CPSR, 0x10)
        for i, reg in enumerate(ARGS):
            self.cpu.reg_write(reg, (arguments[i] if i < len(arguments) else 0) & MASK32)
        sp = STACK+0x8000
        self.write(sp, struct.pack('<'+'I'*max(0, len(arguments)-4),
                                  *(v & MASK32 for v in arguments[4:])))
        self.cpu.reg_write(UC_ARM_REG_SP, sp)
        self.cpu.reg_write(UC_ARM_REG_LR, STOP)
        for i, reg in enumerate(SAVED):
            self.cpu.reg_write(reg, 0x12340000+i)
        try:
            self.cpu.emu_start(address, STOP, count=instructions)
        except UcError as error:
            raise RuntimeError(f'CPU at {self.cpu.reg_read(UC_ARM_REG_PC):08x}; entry {address:08x}, arguments {arguments}: {error}') from error
        if fail:
            assert self.failed, 'Native invalid input did not fail explicitly'
        else:
            assert self.failed is None, self.failed
            assert self.cpu.reg_read(UC_ARM_REG_PC) == STOP, 'CPU routine did not return'
            assert self.cpu.reg_read(UC_ARM_REG_SP) == sp, 'SP changed'
            assert all(self.cpu.reg_read(reg) == 0x12340000+i
                       for i, reg in enumerate(SAVED)), 'ARM callee-saved registers changed'
        return self.cpu.reg_read(UC_ARM_REG_R0)


def compile_sources(temp, generated, tables, arm, sanitizer=False):
    flags = ['-O2', '-Wall', '-Wextra', '-Werror', '-fno-strict-aliasing',
             '-ffunction-sections', '-fdata-sections', '-I'+str(ROOT/'include')]
    if arm:
        flags += ['-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft',
                  '-Wa,--noexecstack']
    elif sanitizer:
        flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    sources = [*[ROOT/p for p in NATIVE], generated, tables,
               ROOT/('tests/native_menu_math_failure_stub.cpp' if arm else 'tests/test_native_matrix_submit.cpp')]
    if not arm:
        sources.append(ROOT/'source/vendor/ntr/gx.cpp')
    objects = []
    for i, source in enumerate(sources):
        c = source.suffix == '.c'
        compiler = TOOL+('gcc' if c else 'g++') if arm else ('cc' if c else 'c++')
        obj = temp/(('arm-' if arm else 'host-')+str(i)+'.o')
        subprocess.run([compiler, *flags, '-std=c11' if c else '-std=gnu++20',
                        '-c', str(source), '-o', str(obj)], check=True, cwd=ROOT)
        objects.append(str(obj))
    executable = temp/('matrix.elf' if arm else 'matrix-host')
    link = [TOOL+'g++' if arm else 'c++', *flags]
    if arm:
        roots = [LOOK, PERSPECTIVE, CAMERA, 'sm64ds_draw_reset', 'sm64ds_draw_count',
                 'sm64ds_draw_entries', 'sm64ds_draw_ok', 'native_video_reset']
        # native_video_reset's C++ name is resolved explicitly, while the test
        # observes draw symbols through their production C ABI names.
        roots[-1] = '_ZN6sm64ds18native_video_resetEv'
        link += ['-nostartfiles', '-Wl,--gc-sections,-z,noexecstack,-Ttext=0x100000,-e,'+LOOK,
                 *['-Wl,-u,'+name for name in roots]]
    subprocess.run([*link, *objects, '-o', str(executable)], check=True, cwd=ROOT)
    return executable


def load_elf(path):
    native = Cpu()
    raw = path.read_bytes()
    phoff = struct.unpack_from('<I', raw, 28)[0]
    phsize, phnum = struct.unpack_from('<HH', raw, 42)
    for i in range(phnum):
        kind, offset, va, _, filesz, memsz, _, _ = struct.unpack_from('<8I', raw, phoff+i*phsize)
        if kind == 1 and memsz:
            native.map(va, memsz)
            native.write(va, raw[offset:offset+filesz])
    symbols = {parts[2]: int(parts[0], 16) for line in subprocess.check_output(
        [TOOL+'nm', '--defined-only', str(path)], text=True).splitlines()
        if len(parts := line.split()) == 3}
    def fatal(uc, address, size, user):
        native.failed = native.read(uc.reg_read(UC_ARM_REG_R0), 200).split(b'\0', 1)[0].decode()
        uc.emu_stop()
    native.cpu.hook_add(UC_HOOK_CODE, fatal,
                       begin=symbols['sm64ds_native_missing'], end=symbols['sm64ds_native_missing'])
    return native, symbols


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path, default=ROOT/'build/roms/europe.nds')
    parser.add_argument('--report', type=Path, default=ROOT/'build/native-runtime/matrix-submit-test.json')
    args = parser.parse_args()
    pin = subprocess.check_output(['git', '-C', str(ROOT/'upstream'), 'rev-parse', 'HEAD'], text=True).strip()
    assert pin == PIN
    for name, digest in REVIEWED.items():
        assert sha(ROOT/'upstream/src'/name) == digest, 'Reviewed source changed: '+name
    raw = args.rom.read_bytes()
    assert zlib.crc32(raw) == 0x29715dec, 'Requires verified ASMP0 ROM'
    rom = ndspy.rom.NintendoDSRom(raw)
    assert rom.idCode == b'ASMP' and rom.version == 0
    arm9 = ndspy.codeCompression.decompress(rom.arm9)
    overlay = rom.loadArm9Overlays([7])[7]
    trig = struct.unpack_from('<8192h', arm9, 0x02082214-rom.arm9RamAddress)
    original = Cpu()
    original.map(rom.arm9RamAddress, len(arm9));original.write(rom.arm9RamAddress, arm9)
    relocated_sections = []
    for section in ndspy.code.MainCodeFile(rom.arm9,rom.arm9RamAddress).sections:
        if section.ramAddress < 0x02000000 and section.data:
            original.map(section.ramAddress,len(section.data))
            original.write(section.ramAddress,section.data)
            relocated_sections.append({'address':hex(section.ramAddress),'bytes':len(section.data),
                'sha256':hashlib.sha256(section.data).hexdigest()})
    original.map(overlay.ramAddress, len(overlay.data));original.write(overlay.ramAddress, overlay.data)
    original.map(0x04000000, 4096, 3)
    io, ports = [], []
    def read_io(uc, access, address, size, value, user):
        io.append(('read', address, size))
        if address in (0x040002a0, 0x040002a4):
            numerator = struct.unpack('<q', uc.mem_read(0x04000290, 8))[0]
            mode = struct.unpack('<H', uc.mem_read(0x04000280, 2))[0] & 3
            denominator = (struct.unpack('<i', uc.mem_read(0x04000298, 4))[0] if mode == 1
                           else struct.unpack('<q', uc.mem_read(0x04000298, 8))[0])
            quotient = (-1 if numerator >= 0 else 1) if denominator == 0 else \
                (abs(numerator)//abs(denominator))*(-1 if (numerator < 0) != (denominator < 0) else 1)
            uc.mem_write(0x040002a0, struct.pack('<Q', quotient & MASK64))
        elif address == 0x040002b4:
            radicand = struct.unpack('<Q', uc.mem_read(0x040002b8, 8))[0]
            if not struct.unpack('<H', uc.mem_read(0x040002b0, 2))[0] & 1:
                radicand &= MASK32
            uc.mem_write(address, struct.pack('<I', math.isqrt(radicand)))
    def write_io(uc, access, address, size, value, user):
        io.append(('write', address, size))
        if 0x04000440 <= address <= 0x040005c8:
            assert size == 4 and not address & 3
            ports.append(((address-0x04000400)//4, value & MASK32))
    original.cpu.hook_add(UC_HOOK_MEM_READ, read_io, begin=0x04000000, end=0x04000fff)
    original.cpu.hook_add(UC_HOOK_MEM_WRITE, write_io, begin=0x04000000, end=0x04000fff)
    counts = Counter();audit = {}
    def audit_io(name):
        unique = Counter(io)
        audit[name] = [{'operation': op, 'address': hex(address), 'bytes': size, 'count': count}
                       for (op, address, size), count in sorted(unique.items())]
    rng = random.Random(0x55a64)
    cases = [((0,0,0),(0,4096,0),(0,0,4096)),
             ((4096,8192,12288),(0,4096,0),(4096,8192,16384)),
             ((0,0,0),(0,0,0),(0,0,0)), ((4096,0,0),(4096,0,0),(0,0,0)),
             ((-2147483648,1,4096),(0,4096,0),(2147483647,-1,-4096))]
    cases.extend(tuple(tuple(rng.randint(-1000000,1000000) for _ in range(3))
                       for _ in range(3)) for _ in range(80))
    fixtures = bytearray()
    with tempfile.TemporaryDirectory(prefix='sm64ds-matrix-submit-') as directory:
        temp = Path(directory)
        body, replacements = native_scene_video.adapt((ROOT/'upstream/src'/f'{CAMERA}.c').read_text(), 2)
        generated = temp/'camera.cpp'
        generated.write_text('#include "sm64ds/native_register.hpp"\nextern "C" {\n'+body+'\n}\n')
        tables = temp/'trig.cpp'
        tables.write_text('extern "C" { short data_02082214[8192]={' + ','.join(map(str,trig)) + '}; }\n')
        elf = compile_sources(temp, generated, tables, True)
        native, symbols = load_elf(elf)
        def reset_stream():
            native.invoke(symbols['sm64ds_draw_reset'])
        def native_words():
            count = native.invoke(symbols['sm64ds_draw_count'])
            assert native.invoke(symbols['sm64ds_draw_ok']) == 1
            pointer = native.invoke(symbols['sm64ds_draw_entries'])
            return list(struct.iter_unpack('<II', native.read(pointer, count*8)))
        for index, vectors in enumerate(cases):
            for draw in (0,1):
                for output in (0, 0x140, 0x40, 0x80, 0xc0):
                    image = bytearray([0xa5]*768)
                    for offset, vector in zip((0x40,0x80,0xc0), vectors):
                        struct.pack_into('<3i', image, offset, *vector)
                    arguments = (BUFFER+0x40, BUFFER+0x80, BUFFER+0xc0, draw, BUFFER+output if output else 0)
                    reset_stream();native.write(BUFFER,image);original.write(BUFFER,image)
                    native.invoke(symbols[LOOK], arguments)
                    io.clear();ports.clear();original.invoke(0x02055a64, arguments)
                    assert native.read(BUFFER,768) == original.read(BUFFER,768), ('LookAt matrix/canary',index,draw,output)
                    words = native_words()
                    assert words == ports and len(words) == (13 if draw else 0), ('LookAt stream',index,draw,output)
                    if index == 0 and draw == 1 and output == 0x140:
                        audit_io('LookAt including two NormalizeVec3 calls')
                    if draw == 1 and output == 0x140:
                        fixtures += struct.pack('<9i', *(v for vector in vectors for v in vector))
                        fixtures += original.read(BUFFER+output,48)
                    counts['LookAt matrix/stream/ABI'] += 1
        projection_cases = [(0x579,0xf09,0x1555,0x1000,0x190000,0x1000),
                            (0x800,0xddb,0x1555,0x1000,0x1388000,0x12bb),
                            (0,4096,0,4096,4096,4096),
                            (4096,-4096,-4096,-4096,4096,-4096)]
        projection_cases.extend((trig[2*i],trig[2*i+1],rng.choice((0x1000,0x1555,0x2000)),
                                  rng.choice((0x100,0x1000,0x4000)),rng.choice((0x190000,0x1388000)),
                                  rng.choice((0x800,0x1000,0x12bb,0x2000))) for i in range(1,33))
        for index, values in enumerate(projection_cases):
            for draw in (0,1):
                for store in (False,True):
                    image = bytes([0xa5]*256);arguments = (*values,draw,BUFFER+32 if store else 0)
                    reset_stream();native.write(BUFFER,image);original.write(BUFFER,image)
                    native.invoke(symbols[PERSPECTIVE],arguments)
                    io.clear();ports.clear();original.invoke(0x02055bfc,arguments)
                    assert native.read(BUFFER,256) == original.read(BUFFER,256), ('Perspective',index,values,draw,store)
                    words = native_words()
                    assert words == ports and len(words) == (17 if draw else 0), ('Perspective stream',index,values)
                    if index == 0 and draw == 1 and store:
                        audit_io('PerspectiveW including divider calls')
                    counts['Perspective matrix/stream/ABI'] += 1
        for index, vectors in enumerate(cases[:20]):
            # +4 aligns the original camera's 64-bit aspect at +0xbc. Inputs
            # are normal game records, with two adjacent view/projection stores.
            image = bytearray([0xa5]*256);c = BUFFER+4
            for offset, vector in zip((8,0x20,0x14), vectors):
                struct.pack_into('<3i',image,4+offset,*vector)
            image[4+0xb8:4+0xbc] = bytes((0,255,0,191))
            struct.pack_into('<q',image,4+0xbc,0x155555555)
            struct.pack_into('<ii',image,4+0xc4,0x1000,0x190000)
            struct.pack_into('<H',image,4+0xd4,0x4da+(index%8)*16)
            reset_stream();native.write(BUFFER,image);original.write(BUFFER,image)
            native.invoke(symbols[LOOK],(c+8,c+0x20,c+0x14,1,c+0x44))
            native.invoke(symbols[CAMERA],(c,))
            io.clear();ports.clear();original.invoke(0x02055a64,(c+8,c+0x20,c+0x14,1,c+0x44))
            original.invoke(0x020c0078,(c,))
            assert native.read(BUFFER,256) == original.read(BUFFER,256), ('camera view/projection/canaries',index)
            words = native_words();assert words == ports and len(words) == 32, ('complete camera stream',index)
            if index == 0:
                audit_io('LookAt then OV007 camera perspective/viewport/store')
            counts['camera complete 32-word stream/ABI'] += 1
        for pointer in (0,1,2):
            arguments = [BUFFER+64,BUFFER+128,BUFFER+192,0,0];arguments[pointer] = 0
            native.invoke(symbols[LOOK],arguments,True)
            counts['null input explicit failure'] += 1
        fixture = temp/'rom-matrices.bin';fixture.write_bytes(fixtures)
        host = compile_sources(temp, generated, tables, False, True)
        completed = subprocess.run([str(host),str(fixture)], text=True, capture_output=True, cwd=ROOT)
        if completed.returncode:
            raise RuntimeError(completed.stdout+completed.stderr)
        assert not completed.stderr, completed.stderr
        host_result = json.loads(completed.stdout)
        elf_sha = sha(elf)
    report = {
        'status':'passed','scope':'ARM11/ARM9 CPU oracle and real native GX decoder on host',
        'upstream_commit':PIN,'source_rom_crc32':'29715dec','source_rom_sha256':hashlib.sha256(raw).hexdigest(),
        'reviewed_upstream_source_sha256':REVIEWED,
        'original_ROM_relocated_code_sections':relocated_sections,
        'native_source_sha256':{path:sha(ROOT/path) for path in NATIVE},
        'native_elf_sha256':elf_sha,'comparisons':dict(counts),'host_sanitized_GX_checks':host_result,
        'generated_camera_sites':replacements,'original_complete_IO_audit':audit,
        'native_DS_IO_mapping':False,'native_unexpected_IRQ_or_GXSTAT':'explicit failure',
        'sanitizers':['address','undefined'],'ABI':'SP/r4-r11 and buffer canaries verified',
        'Azahar_run':False,'physical_hardware':False,'playability_verified':False,'target_performance_verified':False,
    }
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({key:report[key] for key in ('status','scope','comparisons','host_sanitized_GX_checks','ABI')},indent=2))
    print('Report: '+str(args.report))


if __name__ == '__main__':
    main()
