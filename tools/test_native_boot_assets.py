#!/usr/bin/env python3
"""Regression for the real boot graphics and their ARM11 decompressor.

Requires the user's ASMP0 asset pack, ndspy and Unicorn. Reports hashes and
counts only; no ROM assets are distributed. This is CPU/host evidence, not a
3DS gameplay or performance test. An optional previous attempt establishes
the corruption's cause using that exact executable and its recorded VRAM.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

import ndspy.codeCompression
import ndspy.lz10
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_SP,
                               UC_ARM_REG_LR, UC_ARM_REG_PC)
import build_native_runtime as runtime

ROOT = Path(__file__).resolve().parents[1]
TOOL = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
# Starts and physical VRAM destinations follow BootScene::InitResources.
STREAMS = [(0x02091570, 0x020916d8, (0x41800,)),
           (0x020916d8, 0x020918c4, (0x1b800,)),
           (0x020918c4, 0x02092110, (0x10000, 0x50000))]


def symbols(path):
    rows = subprocess.check_output([TOOL+'nm', '--defined-only', str(path)], text=True)
    return {p[2]: int(p[0], 16) for row in rows.splitlines()
            if len(p := row.split()) == 3}


def read_pack(path):
    raw = path.read_bytes()
    assert raw[:8] == b'SM64PAK1' and raw[16:21] == b'ASMP\0'
    assert struct.unpack_from('<I', raw, 28)[0] == 0x29715dec
    assert zlib.crc32(raw[:40]) == struct.unpack_from('<I', raw, 40)[0]
    count = struct.unpack_from('<I', raw, 12)[0]
    index = raw[64:64+272*count]
    assert zlib.crc32(index) == struct.unpack_from('<I', raw, 36)[0]
    files = {}
    for at in range(0, len(index), 272):
        _, offset, size, crc = struct.unpack_from('<4I', index, at)
        name = index[at+16:at+272].split(b'\0')[0].decode()
        if name not in ('__rom/arm9.bin', '__rom/header.bin'):
            continue
        data = raw[offset:offset+size]
        assert len(data) == size and zlib.crc32(data) == crc
        files[name] = data
    return (ndspy.codeCompression.decompress(files['__rom/arm9.bin']),
            struct.unpack_from('<I', files['__rom/header.bin'], 0x28)[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack', type=Path, default=ROOT/'build/europe.pak')
    parser.add_argument('--previous-attempt', type=Path)
    parser.add_argument('--report', type=Path, default=ROOT/'build/native-runtime/boot-assets-test.json')
    args = parser.parse_args()
    arm9, base = read_pack(args.pack)
    regions, _, _, relocs, _ = runtime.metadata()
    corrected, exclusions = runtime.exclude_opaque_data_relocations(relocs)
    assert len(relocs)-len(corrected) == len(runtime.OPAQUE_DATA_RELOCATIONS)
    assert sum(e['module'] == 'arm9' for e in exclusions) == 1
    # The rule must reject drift, not silently skip a newly different pointer.
    try:
        runtime.exclude_opaque_data_relocations(corrected)
    except ValueError:
        pass
    else:
        raise AssertionError('Missing reviewed metadata must be rejected')
    for begin, end, _ in STREAMS:
        assert not any(m == 'arm9' and begin <= a < end for m, a, _, _ in corrected)
    # Check the emitted table too: a policy test alone cannot prove the linked
    # bundle stopped relocating these bytes.
    assembly = (ROOT/'build/native-runtime/native_tables.S').read_text()
    section, region = runtime.layout.owner(regions, 'arm9', 0x02091ae4)
    bad_prefix = f'.word {section},{0x02091ae4-region["start"]},'
    assert not any(line.startswith(bad_prefix) for line in assembly.splitlines())
    with tempfile.TemporaryDirectory(prefix='sm64ds-boot-assets-') as tmp:
        tmp = Path(tmp)
        subprocess.run([TOOL+'gcc', *runtime.FLAGS, '-c',
                        ROOT/'source/ctr/game_memory_primitives.S', '-o', tmp/'memory.o'], check=True)
        subprocess.run([TOOL+'ld', '-Ttext=0x100000', '-e', 'DecompressLZ16',
                        tmp/'memory.o', '-o', tmp/'memory.elf'], check=True)
        subprocess.run([TOOL+'objcopy', '-O', 'binary', tmp/'memory.elf', tmp/'memory.bin'], check=True)
        address = symbols(tmp/'memory.elf')['DecompressLZ16']
        binary = (tmp/'memory.bin').read_bytes()
    cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    for address_base in (0x100000, 0x200000, 0x300000, 0x400000):
        cpu.mem_map(address_base, 0x10000)
    cpu.mem_write(0x100000, binary)
    results = []
    for begin, end, _ in STREAMS:
        packed = arm9[begin-base:end-base]
        expected = ndspy.lz10.decompress(packed)
        cpu.mem_write(0x200000, packed)
        cpu.mem_write(0x300000, b'\xa5'*(len(expected)+32))
        cpu.reg_write(UC_ARM_REG_R0, 0x200000)
        cpu.reg_write(UC_ARM_REG_R1, 0x300010)
        cpu.reg_write(UC_ARM_REG_SP, 0x408000)
        cpu.reg_write(UC_ARM_REG_LR, 0x110000)
        cpu.emu_start(address, 0x110000, count=1000000)
        assert cpu.reg_read(UC_ARM_REG_PC) == 0x110000
        assert cpu.reg_read(UC_ARM_REG_SP) == 0x408000
        actual = bytes(cpu.mem_read(0x300000, len(expected)+32))
        assert actual == b'\xa5'*16 + expected + b'\xa5'*16
        results.append({'source': hex(begin), 'decompressed_bytes': len(expected),
                        'sha256': hashlib.sha256(expected).hexdigest()})
    report = {'status': 'passed', 'scope': 'Host and isolated ARM11 instructions',
              'physical_hardware': False, 'playability_verified': False,
              'streams': results, 'opaque_relocation_exclusions': exclusions}
    if args.previous_attempt:
        attempt = args.previous_attempt
        # Resolve the erroneous replacement exactly as the historical binary
        # did. Never assume the latest link has the same native addresses.
        sec, region = runtime.layout.owner(regions, 'ov085', 0x0212ffff)
        replacement = symbols(attempt/'sm64ds-native.elf')[runtime.layout.section_name(sec)]
        replacement += 0x0212ffff-region['start']
        damaged = bytearray(arm9[0x020918c4-base:0x02092110-base])
        struct.pack_into('<I', damaged, 0x02091ae4-0x020918c4, replacement)
        expected = ndspy.lz10.decompress(arm9[0x020918c4-base:0x02092110-base])
        reproduced = ndspy.lz10.decompress(damaged)
        vram = (attempt/'boot-video.bin').read_bytes()[0x2800:]
        assert all(vram[p:p+len(reproduced)] == reproduced for p in (0x10000, 0x50000))
        report['historical_corruption'] = {
            'attempt': str(attempt), 'reproduces_both_vram_banks_exactly': True,
            'damaged_bytes_per_bank': sum(a != b for a, b in zip(expected, reproduced))}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
