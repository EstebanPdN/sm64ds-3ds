#!/usr/bin/env python3
"""Verify the whole ASMP0 embedded SPA and its original/native ARM consumers.

The production loader publishes the corrected native bytes. The original ROM
and this exact ELF then execute the manager constructor, real bump allocator,
321-definition parser and texture/palette upload callers in both capacities.
Only VRAM allocation and SDK uploads are controlled observations. Typed pointer
fields are checked before normalization; address-looking serialized words are
never normalized. No Azahar, frame, particle simulation or hardware evidence.
"""
import argparse
import json
from pathlib import Path
import struct

import build_native_runtime as runtime
import test_native_audio_deferred_arm as shared
import test_native_menu_assets as assets
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_PC, UC_ARM_REG_LR

base, ROOT = shared.base, shared.ROOT
SPA, SIZE, DIGEST = 0x02075f14, 0xc214, '920804d76c21e5e147c9ec0d85f32e3648416586711a1ca2708d0668241e62fb'
ARENA = shared.WORK+0x1000
PARTS = [(8, 12), (9, 12), (10, 8), (11, 12), (16, 20)]
BEHAVIORS = [(24, 8, 'Acceleration'), (25, 8, 'Jitter'),
             (26, 16, 'Converge'), (27, 4, 'Turn'),
             (28, 8, 'LimitPlane'), (29, 16, 'RadiusConverge')]


def parse(raw):
    assert len(raw) == SIZE and base.sha(raw) == DIGEST
    assert raw[:8] == b' APS10_1'
    count, textures = struct.unpack_from('<HH', raw, 8)
    assert (count, textures) == (321, 46)
    definition_bytes, texture_bytes, texture_offset = struct.unpack_from('<3I', raw, 16)
    cursor, entries = 32, []
    for index in range(count):
        start = cursor
        assert cursor+0x38 <= texture_offset
        flags = struct.unpack_from('<I', raw, cursor)[0]
        cursor += 0x38
        parts, behaviors = [], []
        for bit, size in PARTS:
            parts.append(cursor if flags & (1 << bit) else None)
            if flags & (1 << bit): cursor += size
        for bit, size, name in BEHAVIORS:
            if flags & (1 << bit):
                behaviors.append((name, cursor)); cursor += size
        assert cursor <= texture_offset
        entries.append(dict(index=index, offset=start, flags=flags,
                            parts=parts, behaviors=behaviors, end=cursor))
    assert cursor == texture_offset == definition_bytes+32 == 0x6e60
    records = []
    for index in range(textures):
        assert cursor+32 <= len(raw)
        words = struct.unpack_from('<8I', raw, cursor)
        flags, pixel_size, palette_offset, palette_size, length = words[1:5]+(words[7],)
        assert 32 <= length <= len(raw)-cursor
        assert pixel_size <= length-32
        if palette_size:
            assert 32 <= palette_offset and palette_size <= length-palette_offset
        records.append(dict(index=index, offset=cursor, flags=flags,
            pixel_size=pixel_size, palette_offset=palette_offset,
            palette_size=palette_size, length=length))
        cursor += length
    assert cursor == len(raw) == texture_offset+texture_bytes
    assert sum(len(e['behaviors']) for e in entries) == 280
    assert [(e['index'], e['offset'], e['flags']) for e in entries[306:308]] == [
        (306, 0x6920, 0x02083620), (307, 0x6974, 0x02083620)]
    return entries, records


def verify_relocations(raw, tables):
    regions, _, _, relocations, _ = runtime.metadata()
    corrected, exclusions = runtime.exclude_opaque_data_relocations(relocations)
    reviewed = [('arm9', SPA+offset, 'arm9', 0x02083620) for offset in (0x6920, 0x6974)]
    in_resource = [row for row in relocations if row[0] == 'arm9' and SPA <= row[1] < SPA+SIZE]
    assert sorted(in_resource) == sorted(reviewed)
    emitted = assets.emitted_fixups(tables)
    for row in reviewed:
        assert struct.unpack_from('<I', raw, row[1]-SPA)[0] == row[3]
        assert row in relocations and row not in corrected
        index, region = runtime.layout.owner(regions, row[0], row[1])
        assert (index, row[1]-region['start']) not in emitted
    assert not any(m == 'arm9' and SPA <= source < SPA+SIZE for m, source, _, _ in corrected)
    # Valid pointers outside the serialized resource must remain relocatable.
    references = [row for row in relocations if row[0] == 'arm9' and
                  not SPA <= row[1] < SPA+SIZE and row[2:] == ('arm9', 0x02083620)]
    for row in references:
        assert row in corrected
    return dict(excluded_flags=[hex(r[1]) for r in reviewed],
                retained_real_data_references=len(references),
                all_serialized_bytes_covered=True, total_reviewed_exclusions=len(exclusions))


def behavior_name(name):
    return f'_ZN8Particle{len(name)}{name}4FuncERNS_10EffectDataEPcR7Vector3'


def canonical_arena(probe, manager, spa, entries, records, capacity):
    arena = bytearray(probe.cpu.mem_read(ARENA, capacity))
    def word(address, expected, canonical):
        assert probe.u32(address) == expected, (probe.label, hex(address), hex(expected), hex(probe.u32(address)))
        struct.pack_into('<I', arena, address-ARENA, canonical)
    word(manager, probe.names['func_02023178'], 0)
    particles, textures = probe.u32(manager+0x1c), probe.u32(manager+0x20)
    assert struct.unpack('<HH', probe.cpu.mem_read(manager+0x24, 4)) == (321, 46)
    for entry in entries:
        address = particles+entry['index']*32
        for slot, offset in enumerate([entry['offset'], *entry['parts']]):
            word(address+slot*4, 0 if offset is None else spa+offset,
                 0 if offset is None else SPA+offset)
        count = struct.unpack('<H', probe.cpu.mem_read(address+28, 2))[0]
        assert count == len(entry['behaviors'])
        behavior = probe.u32(address+24)
        assert bool(behavior) == bool(count)
        for i, (name, offset) in enumerate(entry['behaviors']):
            function = behavior_name(name)
            word(behavior+i*8, probe.names[function], next(bit for bit, _, n in BEHAVIORS if n == name))
            word(behavior+i*8+4, spa+offset, SPA+offset)
    for record in records:
        word(textures+record['index']*20, spa+record['offset'], SPA+record['offset'])
    return bytes(arena)


def execute(probe, spa, raw, entries, records, mode):
    count_a, count_b, capacity = mode
    probe.cpu.mem_write(spa, raw)
    probe.cpu.mem_write(ARENA-32, bytes([0xa5])*(capacity+64))
    probe.words(probe.names['data_0209ee78'], ARENA)
    probe.words(probe.names['data_0209ee7c'], ARENA+capacity)
    allocations, uploads = [], []
    cursors = dict(texture=0x20000, palette=0x8000)
    callbacks = {'_ZN8Particle7Texture12AllocTexVramEjb': 'texture',
                 '_ZN8Particle7Texture12AllocPalVramEjb': 'palette'}
    sdk = {'_ZN2GX7LoadTexEPKvjj': 'texture', '_ZN2GX11LoadTexPlttEPKvjj': 'palette'}
    transitions = {'_ZN2GX12BeginLoadTexEv', '_ZN2GX10EndLoadTexEv',
                   '_ZN2GX16BeginLoadTexPlttEv', '_ZN2GX14EndLoadTexPlttEv'}
    watched = {probe.names[name]: name for name in [*callbacks, *sdk, *transitions, 'func_02023178']}
    def observe(cpu, address, size, _):
        name = watched.get(address)
        if name == 'func_02023178':
            length = cpu.reg_read(shared.REGS[0]); start = probe.u32(probe.names['data_0209ee78'])
            assert ARENA <= start and length <= ARENA+capacity-start
            allocations.append((start, length)); return
        if name in callbacks:
            kind = callbacks[name]; length, flag = [cpu.reg_read(r) for r in shared.REGS[:2]]
            assert length <= SIZE and flag in (0, 1)
            result = cursors[kind]; cursors[kind] += (length+15)&~15
            uploads.append(dict(callback=kind, bytes=length, flag=flag, result=result))
            cpu.reg_write(UC_ARM_REG_R0, result)
        elif name in sdk:
            source, target, length = [cpu.reg_read(r) for r in shared.REGS[:3]]
            assert spa <= source and length <= spa+SIZE-source
            uploads.append(dict(upload=sdk[name], source_offset=source-spa,
                target=target, bytes=length, sha256=base.sha(bytes(cpu.mem_read(source, length)))))
        elif name in transitions:
            uploads.append(dict(transition=name))
        else: return
        cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
    hook = probe.cpu.hook_add(UC_HOOK_CODE, observe)
    manager = probe.call('func_0204a4c8', [probe.names['func_02023178'], count_a, count_b, 0x1a, 0x3e])
    assert manager == ARENA
    probe.call('func_0204a17c', [manager, spa])
    used = probe.u32(probe.names['data_0209ee78'])-ARENA
    expected = 0x3c+count_a*0x78+count_b*0x44+321*32+280*8+46*20
    assert used == expected < capacity
    before = canonical_arena(probe, manager, spa, entries, records, capacity)
    assert probe.call('func_0204a0dc', [manager, probe.names['_ZN8Particle7Texture12AllocTexVramEjb']]) == 1
    assert probe.call('func_0204a028', [manager, probe.names['_ZN8Particle7Texture12AllocPalVramEjb']]) == 1
    after = canonical_arena(probe, manager, spa, entries, records, capacity)
    assert bytes(probe.cpu.mem_read(ARENA-32, 32)) == bytes([0xa5])*32
    assert bytes(probe.cpu.mem_read(ARENA+capacity, 32)) == bytes([0xa5])*32
    assert bytes(probe.cpu.mem_read(spa, SIZE)) == raw
    assert sum(row.get('upload') == 'texture' for row in uploads) == 46
    assert sum(row.get('upload') == 'palette' for row in uploads) == sum(bool(r['palette_size']) for r in records)
    probe.cpu.hook_del(hook)
    return dict(capacity=capacity, required=used, free_bytes=capacity-used,
        allocation_requests=allocations, uploads=uploads, before=before, after=after)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--tables', type=Path, default=ROOT/'build/native-runtime/native_tables.S')
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args(); elf_hash = base.sha(args.elf.read_bytes())
    cart = base.Cartridge(ROOT/'build/europe.pak')
    original_base, _, image = cart.image(0xffffffff)
    raw = image[SPA-original_base:SPA-original_base+SIZE]
    entries, records = parse(raw)
    relocations = verify_relocations(raw, args.tables)
    machine = base.Machine(args.elf, cart)
    entry = next(n for n in machine.names if n.startswith('_ZN6sm64ds19native_runtime_load'))
    assert machine.call(entry, machine.pack, machine.error) == 1
    native_spa = machine.names['data_02075f14']
    published = bytes(machine.cpu.mem_read(native_spa, SIZE))
    assert published == raw, 'Production loader changed serialized SPA bytes'
    original = shared.Probe('original ASMP0 embedded SPA consumers', cart=cart)
    native = shared.Probe('corrected production ARM11 embedded SPA consumers', elf=args.elf)
    cases = []
    for mode in ((40, 256, 0x8c00), (64, 320, 0xa800)):
        left = execute(original, SPA, raw, entries, records, mode)
        right = execute(native, native_spa, published, entries, records, mode)
        assert left == right, 'Original/native manager arena or upload contract differs'
        cases.append({k: (base.sha(v) if isinstance(v, bytes) else v) for k, v in right.items()})
    assert base.sha(args.elf.read_bytes()) == elf_hash
    report = dict(status='PASS', scope=__doc__, ELF_sha256=elf_hash,
        production_loader_executed=True, production_SPA_byte_identical=True,
        SPA_bytes=SIZE, SPA_sha256=DIGEST, definitions=321, textures=46, behaviors=280,
        relocations=relocations, cases=cases,
        boundaries=['verified cartridge image/catalog backend', 'bounded observer C++ allocations for loader',
                    'controlled VRAM allocators', 'SDK upload observations'],
        canaries_and_call_ABI=True, cartridge_bytes_exported=False, Azahar_launched=False,
        physical_hardware=False)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(dict(status='PASS', report=str(args.report), ELF_sha256=elf_hash,
        SPA_bytes=SIZE, definitions=321, textures=46, capacities=len(cases)), indent=2))


if __name__ == '__main__': main()
