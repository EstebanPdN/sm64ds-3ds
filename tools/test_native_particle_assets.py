#!/usr/bin/env python3
"""Compare bounded original DSMT SPA parsing, upload callbacks and bindings.

Runs the actual ROM and ARM11 ELF paths b782c, b77ac, two b7708 additions and
49ee8 -> 4a5c8 -> 4af3c. The two newly created systems have no particle children;
this checks material setup before particle simulation, not particle drawing.
SDK uploads and native GX submission are observed at their service boundaries.
This is host CPU/ABI evidence, not an original rendered frame or console test.
"""
import argparse
import json
from pathlib import Path
import struct

import test_native_menu_constructors as constructors
from unicorn import UC_HOOK_MEM_WRITE, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_PC, UC_ARM_REG_LR

base, ROOT = constructors.base, constructors.ROOT
SPA, POSITION, MATRIX = 0x123c0000, 0x12400000, 0x12400020
ASSET = 'data/DSMT/mdst_kira1_spa.bin'
ASSET_SHA = '6407bacff4838b43343468ad7507a5735e33dda8e0cb9c74edf4bcdeeba82596'
UPLOADS = [('texture', '_ZN2GX7LoadTexEPKvjj'),
           ('palette', '_ZN2GX11LoadTexPlttEPKvjj')]
TRANSITIONS = ['_ZN2GX12BeginLoadTexEv', '_ZN2GX10EndLoadTexEv',
               '_ZN2GX16BeginLoadTexPlttEv', '_ZN2GX14EndLoadTexPlttEv']
EXPECTED_BINDING = [('0x40004a8', 0x689072b0, 4),
                    ('0x40004ac', 0xb8, 4), ('0x4000440', 3, 4),
                    ('0x4000454', 0, 4), ('0x400046c', 65536, 4),
                    ('0x400046c', 65536, 4), ('0x400046c', 0, 4),
                    ('0x4000440', 1, 4)]


def normalize_allocator(probe, snapshot):
    """Check and remove only the known architecture-specific callback field."""
    arena = probe.u32(probe.names['data_ov007_02103448'])
    manager = probe.u32(probe.names['data_ov007_0210344c'])
    assert arena == manager == snapshot['allocations'][0][0]
    assert snapshot['allocations'] == [(arena, 0x3c00)]
    block = bytearray(snapshot['blocks'][0])
    assert struct.unpack_from('<I', block, 0)[0] == probe.names['func_ov007_020b78d0']
    block[:4] = bytes(4)
    snapshot['blocks'][0] = bytes(block)


def compare(original, native, left, right):
    normalize_allocator(original, left)
    normalize_allocator(native, right)
    errors = constructors.differences(left, right, compare_return=False)
    assert not errors, errors
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path,
                        default=ROOT/'build/3ds/source/ctr/sm64ds-native.elf')
    parser.add_argument('--report', type=Path,
                        default=ROOT/'build/native-runtime/particle-assets-test.json')
    args = parser.parse_args()
    elf_sha256 = base.sha(args.elf.read_bytes())
    # Reuse machine/publication setup only; the constructor suite is not run.
    harness = constructors.Harness(args.elf)
    original, native = harness.original, harness.native
    spa = harness.cart.file(ASSET)
    assert len(spa) == 320 and base.sha(spa) == ASSET_SHA
    _, indices = harness.cart.handles()
    assert harness.cart.entries[indices[658]][4] == ASSET
    stages = []
    for probe in (original, native):
        probe.cpu.mem_map(0x04000000, 0x2000)
        probe.cpu.mem_write(SPA, spa)
        probe.cpu.mem_write(POSITION, bytes(12))
        probe.cpu.mem_write(MATRIX, struct.pack('<12i',
                             4096, 0, 0, 0, 4096, 0, 0, 0, 4096, 0, 0, 0))
        probe.uploads, probe.words = [], []

        def load(p=probe):
            assert p.reg(0) == 658
            p.observations.append(('real_menu_spa_handle', 658))
            p.cpu.reg_write(UC_ARM_REG_R0, SPA)

        probe.seams[probe.names['LoadFile']] = load
        for name in TRANSITIONS:
            probe.seams[probe.names[name]] = (
                lambda name=name, p=probe: p.uploads.append({'transition': name}))
        for kind, name in UPLOADS:
            def upload(kind=kind, p=probe):
                source, offset, size = p.reg(0), p.reg(1), p.reg(2)
                assert SPA <= source <= SPA+len(spa)
                assert size <= SPA+len(spa)-source
                p.uploads.append({'kind': kind, 'source_offset': source-SPA,
                                  'destination': hex(offset), 'size': size,
                                  'sha256': base.sha(bytes(p.cpu.mem_read(source, size)))})
            probe.seams[probe.names[name]] = upload

        def mmio(cpu, access, address, size, value, _, p=probe):
            if 0x04000400 <= address <= 0x040005c8:
                p.words.append((hex(address), value, size))

        probe.cpu.hook_add(UC_HOOK_MEM_WRITE, mmio)
        if probe is native:
            def adapted(cpu, address, size, _):
                if address == native.names['sm64ds_draw_port']:
                    native.words.append((hex(0x04000400+native.reg(0)*4),
                                         native.reg(1), 4))
                    cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
            native.cpu.hook_add(UC_HOOK_CODE, adapted)
        probe.reset()
        probe.cpu.mem_write(probe.names['data_ov007_02103370'], bytes(0x2e*4))
        probe.cpu.mem_write(probe.names['data_ov007_02103368'],
                             struct.pack('<I', probe.names['func_ov007_020cca74']))

    left = original.call('func_ov007_020b782c', [])
    right = native.call('func_ov007_020b782c', [])
    errors = compare(original, native, left, right)
    stages.append({'name': 'b782c -> 4a4c8 -> 4a17c', 'differences': errors,
                   'normalized_arena_sha256': base.sha(left['blocks'][0])})

    # Controlled cursors isolate the upload callback ABI. The top is inferred
    # from the observed first binding, 0x39580, and its real 64-byte payload.
    # This does not verify the earlier model allocation sequence.
    for probe in (original, native):
        for name, value in [('data_ov007_02104bdc', 0),
                            ('data_ov007_02104be8', 0x5c0),
                            ('data_020a4bdc', 0x395c0), ('data_020a4bd8', 0x4000)]:
            probe.cpu.mem_write(probe.names[name], struct.pack('<I', value))
    left = original.call('func_ov007_020b77ac', [])
    right = native.call('func_ov007_020b77ac', [])
    assert original.uploads == native.uploads
    errors = compare(original, native, left, right)
    uploads = [row for row in original.uploads if 'kind' in row]
    assert [(row['kind'], row['source_offset'], row['destination'], row['size'])
            for row in uploads] == [
                ('texture', 0x90, '0x39580', 64),
                ('texture', 0xf8, '0x39540', 64),
                ('palette', 0xd0, '0x5c0', 8),
                ('palette', 0x138, '0x5c8', 8)]
    assert [row['transition'] for row in original.uploads if 'transition' in row] == TRANSITIONS
    stages.append({'name': 'b77ac -> 4a0dc/4a028 -> b7948/b7914 -> SDK upload observer',
                   'differences': errors, 'uploads': original.uploads})

    for slot in (0, 1):
        left = original.call('func_ov007_020b7708', [slot, POSITION])
        right = native.call('func_ov007_020b7708', [slot, POSITION])
        errors = compare(original, native, left, right)
        stages.append({'name': 'b7708 -> actual AddSystem '+str(slot), 'differences': errors})
    for probe in (original, native):
        manager = probe.u32(probe.names['data_ov007_0210344c'])
        system = probe.u32(manager+4)
        count = 0
        while system:
            assert probe.u32(system+8) == probe.u32(system+0x10) == 0
            count += 1
            assert count <= 2
            system = probe.u32(system)
        assert count == 2
    left = original.call('func_02049ee8',
                         [original.u32(original.names['data_ov007_0210344c']), MATRIX])
    right = native.call('func_02049ee8',
                        [native.u32(native.names['data_ov007_0210344c']), MATRIX])
    errors = compare(original, native, left, right)
    assert original.words == native.words == EXPECTED_BINDING*2
    stages.append({'name': '49ee8 -> 4a5c8 -> 4af3c for both newly added real systems',
                   'differences': errors, 'gx_writes': original.words,
                   'teximage': ['0x689072b0', '0x689072b0'], 'palette': ['0xb8', '0xb8'],
                   'particle_children_per_system': 0})
    assert base.sha(args.elf.read_bytes()) == elf_sha256, 'ELF changed during the comparison'
    result = {'status': 'passed',
              'scope': 'Bounded host CPU/data/upload ABI evidence; no drawn frame, emulator, physical console or performance proof',
              'elf': str(args.elf), 'elf_sha256': elf_sha256,
              'asset': {'path': ASSET, 'handle': 658, 'size': len(spa), 'sha256': ASSET_SHA},
              'seams': ['controlled allocator with canaries',
                        'verified SPA file at its real catalog handle',
                        'SDK begin/load/end upload observations',
                        'GX MMIO/draw-port observations'],
              'upload_cursor_scope': 'Texture top 0x395c0 derived from first real TEXIMAGE; palette start 0x5c0 derived from PLTT; prior model allocations not verified',
              'cartridge_bytes_written_to_disk': False, 'stages': stages}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({'status': 'passed', 'report': str(args.report),
                      'elf_sha256': elf_sha256, 'stages': len(stages),
                      'gx_writes': len(original.words)}, indent=2))


if __name__ == '__main__':
    main()
