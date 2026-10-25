#!/usr/bin/env python3
"""Compare DSMT direction-repeat callers and cstd::mod with the supplied ROM.

Runs original ROM and production ARM11 instructions on a host CPU model. The
original signed-32 DS divider is a documented register observer, not emulated
hardware evidence. A preserved pre-fix ELF must fault on DIVCNT, while the
current provider must match the remainder and original input-repeat selection.
The caller stops at its next state-object operation; no frame, menu usability,
Azahar, physical console or performance result follows from this test. ROM
bytes stay in memory; reports contain addresses, hashes and scalar observations.
"""
import argparse
import json
from pathlib import Path
import random
import re
import struct
import subprocess

import test_native_menu_constructors as constructors
from unicorn import (UcError, UC_HOOK_CODE, UC_HOOK_MEM_READ,
                     UC_HOOK_MEM_WRITE, UC_HOOK_MEM_INVALID, UC_MEM_WRITE)
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR)

base, ROOT = constructors.base, constructors.ROOT
PIN = '6230b2996636e3fd0e03cb178161c94cb4b73cc9'
MOD = '_ZN4cstd3modEii'
KEYS, INPUT, STATE = 0x12500000, 0x12510000, 0x12520000
SOURCES = ['src/d_s_dsmt.c', 'src/_ZN9dScDSMT_c8BehaviorEv.cpp',
    'src/func_ov007_020b7090.c', 'src/func_ov007_020b2e64.c',
    'src/func_ov007_020c20b8.c', 'src/func_ov007_020c22a0.c',
    'src/func_ov007_020c2304.c', 'src/func_ov007_020c1da0.c',
    'src/_ZN4cstd3modEii.cpp']


def remainder(a, b):
    if b == 0:
        return a
    q = (abs(a)//abs(b))*(-1 if (a < 0) != (b < 0) else 1)
    return a-q*b


def words(probe, address, *values):
    probe.cpu.mem_write(address, struct.pack('<'+'I'*len(values),
                                          *(v & 0xffffffff for v in values)))


def leaf_cases(original, native):
    original.cpu.mem_map(0x04000000, 0x1000)
    events = []

    def division_register(cpu, access, address, size, value, _):
        if access == UC_MEM_WRITE:
            events.append(('write', address, size, value))
        else:
            events.append(('read', address, size))
            if address == 0x04000280:
                cpu.mem_write(address, bytes(2))
            elif address == 0x040002a8:
                a = constructors.signed(original.u32(0x04000290))
                b = constructors.signed(original.u32(0x04000298))
                words(original, address, remainder(a, b))

    handle = original.cpu.hook_add(UC_HOOK_MEM_WRITE | UC_HOOK_MEM_READ,
                                   division_register, begin=0x04000280, end=0x040002af)
    edges = [-0x80000000, -0x7fffffff, -21, -10, -1, 0, 1, 10, 0x7fffffff]
    cases = [(a, b) for a in edges for b in edges]
    rng = random.Random(0x534d4453)
    cases += [(rng.randrange(-0x80000000, 0x80000000),
               rng.randrange(-0x80000000, 0x80000000)) for _ in range(512)]
    outputs, selected = [], []
    for a, b in cases:
        events.clear()
        expected = remainder(a, b)
        left = constructors.signed(original.call(MOD, [a & 0xffffffff, b & 0xffffffff])['return'])
        right = constructors.signed(native.call(MOD, [a & 0xffffffff, b & 0xffffffff])['return'])
        assert left == right == expected, (a, b, left, right, expected)
        writes = [row[1:] for row in events if row[0] == 'write']
        # STRD may be reported as one 8-byte store or two 4-byte stores.
        assert writes[:2] == [(0x04000280, 2, 0), (0x04000290, 4, a & 0xffffffff)], writes
        assert any(row[:2] == (0x04000298, 4) or row[:2] == (0x04000298, 8)
                   for row in writes), writes
        assert original.u32(0x04000280) & 3 == 0
        assert original.u32(0x04000298) == b & 0xffffffff
        assert original.u32(0x0400029c) == 0
        assert events[-1] == ('read', 0x040002a8, 4), events
        outputs.append(struct.pack('<iii', a, b, left))
        if (a, b) in [(-21, 10), (21, -10), (-21, -10),
                       (-0x80000000, -1), (-0x80000000, 0), (0, 0), (0x7fffffff, 0)]:
            selected.append({'numerator': a, 'denominator': b, 'remainder': left})
    original.cpu.hook_del(handle)
    original.cpu.mem_unmap(0x04000000, 0x1000)
    return {'cases': len(cases), 'edge_cases': 81, 'seeded_cases': 512,
            'results_sha256': base.sha(b''.join(outputs)), 'selected': selected,
            'rom_division_mode': 0, 'rom_remainder_read': '0x040002a8',
            'original_register_model': 'signed32 a-q*b; div0 remainder=a',
            'native_divider_registers_unmapped': True,
            'normal_returns_preserve_stack_and_callee_saved_registers': True}


def repeat_case(probe, ticks, mask, expect_fault=False):
    probe.cpu.mem_write(KEYS, bytes([0xa5])*0x70)
    probe.call('func_ov007_020c22a0', [KEYS])
    words(probe, probe.names['data_ov007_0210342c'], constructors.CONTEXT)
    words(probe, constructors.CONTEXT+0x54, KEYS)
    words(probe, probe.names['data_ov007_02103360'], STATE)
    probe.cpu.mem_write(STATE, bytes(0x40))
    # c1da0(0) == 0 is the original input-enabled condition.
    words(probe, probe.names['data_ov007_02104bec'], 0)
    words(probe, INPUT, mask)
    for _ in range(ticks-1):
        probe.call('func_ov007_020c20b8', [KEYS, INPUT])
    observations, faults, stopped = [], [], []

    def code(cpu, address, size, _):
        if address == probe.names[MOD]:
            observations.append([constructors.signed(probe.reg(0)),
                                 constructors.signed(probe.reg(1))])
        elif address == probe.names['func_ov007_020c92d0']:
            stopped.append('next_state_object_operation')
            cpu.emu_stop()

    def invalid(cpu, access, address, size, value, _):
        faults.append({'pc': hex(cpu.reg_read(UC_ARM_REG_PC)),
                       'address': hex(address), 'bytes': size,
                       'access': access, 'value': value})
        return False

    code_hook = probe.cpu.hook_add(UC_HOOK_CODE, code)
    invalid_hook = probe.cpu.hook_add(UC_HOOK_MEM_INVALID, invalid)
    probe.cpu.reg_write(UC_ARM_REG_CPSR, 0x10)
    for reg in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3):
        probe.cpu.reg_write(reg, 0)
    stack = base.STACK+0xf000
    probe.cpu.reg_write(UC_ARM_REG_SP, stack)
    probe.cpu.reg_write(UC_ARM_REG_LR, base.STOP)
    words(probe, stack, mask)  # The real b7090 fifth argument lives on the stack.
    try:
        probe.cpu.emu_start(probe.names['func_ov007_020b7090'], base.STOP, count=100000)
    except UcError:
        assert expect_fault and faults, (probe.label, ticks, faults)
    finally:
        probe.cpu.hook_del(code_hook)
        probe.cpu.hook_del(invalid_hook)
    counts = list(struct.unpack('<4I', probe.cpu.mem_read(KEYS+4, 16)))
    direction = {0x40: 0, 0x80: 1, 0x10: 2, 0x20: 3}[mask]
    assert counts == [ticks if i == direction else 0 for i in range(4)], counts
    assert observations == ([[ticks-20, 10]] if ticks >= 20 else [])
    if expect_fault:
        assert not stopped and len(faults) == 1
        assert faults[0]['address'] == '0x4000280' and faults[0]['bytes'] == 2
    else:
        assert not faults and stopped == ['next_state_object_operation']
        selected = constructors.signed(probe.u32(STATE+0x14))
        assert selected == (direction if ticks == 1 or ticks >= 20 and (ticks-20) % 10 == 0 else -1)
    return {'held_updates': ticks, 'held_mask': hex(mask), 'direction_counts': counts,
            'mod_arguments': observations, 'faults': faults, 'boundary': stopped,
            'selection': None if expect_fault else selected,
            'key_object_sha256': base.sha(bytes(probe.cpu.mem_read(KEYS, 0x70)))}


def original_calls(cart, names, caller, callee):
    address = names[caller]
    symbol_text = (ROOT/'upstream/config/arm9/overlays/ov007/symbols.txt').read_text()
    match = re.search(r'^'+re.escape(caller)+r' kind:function\(arm,size=0x([0-9a-f]+)\)', symbol_text, re.M)
    assert match
    start, _, image = cart.image(7)
    sites = []
    for offset in range(0, int(match[1], 16), 4):
        instruction = struct.unpack_from('<I', image, address-start+offset)[0]
        if (instruction >> 24) & 0xf == 0xb:
            imm = instruction & 0xffffff
            target = address+offset+8+((imm if imm < 0x800000 else imm-0x1000000) << 2)
            if target == names[callee]:
                sites.append(hex(address+offset))
    assert sites, (caller, callee)
    return {'caller': caller, 'callee': callee, 'rom_bl_sites': sites}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path,
                        default=ROOT/'build/3ds/source/ctr/sm64ds-native.elf')
    parser.add_argument('--baseline-elf', type=Path,
        default=ROOT/'build/native-runtime-azahar/attempt-1791596592223444000/sm64ds-native.elf')
    parser.add_argument('--report', type=Path,
                        default=ROOT/'build/native-runtime/menu-repeat-test.json')
    args = parser.parse_args()
    assert subprocess.check_output(['git', '-C', str(ROOT/'upstream'), 'rev-parse', 'HEAD'],
                                   text=True).strip() == PIN
    source_hashes = {name: base.sha((ROOT/'upstream'/name).read_bytes()) for name in SOURCES}
    hashes = {label: base.sha(path.read_bytes()) for label, path in
              [('baseline_elf', args.baseline_elf), ('elf', args.elf)]}
    old = constructors.Harness(args.baseline_elf)
    new = constructors.Harness(args.elf)
    original, native = new.original, new.native
    scene_slots = {}
    for slot, name in [(6, '_ZN9dScDSMT_c8BehaviorEv'), (9, '_ZN9dScDSMT_c6RenderEv')]:
        rom = original.u32(original.names['data_ov007_021032e8']+slot*4)
        elf = native.u32(native.names['data_ov007_021032e8']+slot*4)
        assert rom == original.names[name] and elf == native.names[name]
        scene_slots[str(slot)] = {'symbol': name, 'rom_pointer': hex(rom), 'native_pointer': hex(elf)}
    route = [original_calls(new.cart, original.names, caller, callee) for caller, callee in [
        ('_ZN9dScDSMT_c8BehaviorEv', 'func_ov007_020b7090'),
        ('func_ov007_020b7090', 'func_ov007_020c20b8'),
        ('func_ov007_020b7090', 'func_ov007_020b2e64'),
        ('func_ov007_020b2e64', MOD)]]
    negatives = [repeat_case(old.native, 20, mask, expect_fault=True) for mask in (0x40, 0x80, 0x10, 0x20)]
    assert {row['faults'][0]['pc'] for row in negatives} == {hex(old.native.names[MOD]+0x10)}
    contract = leaf_cases(original, native)
    callers = []
    # Keep the original divider register observer for real caller instructions.
    original.cpu.mem_map(0x04000000, 0x1000)
    def divider(cpu, access, address, size, value, _):
        if address == 0x040002a8:
            words(original, address, remainder(constructors.signed(original.u32(0x04000290)),
                                               constructors.signed(original.u32(0x04000298))))
    divider_hook = original.cpu.hook_add(UC_HOOK_MEM_READ, divider, begin=0x040002a8, end=0x040002ab)
    for mask in (0x40, 0x80, 0x10, 0x20):
        for ticks in (1, 19, 20, 21, 29, 30):
            left, right = repeat_case(original, ticks, mask), repeat_case(native, ticks, mask)
            assert left == right, (left, right)
            callers.append(left)
    original.cpu.hook_del(divider_hook)
    assert hashes['elf'] == base.sha(args.elf.read_bytes())
    assert hashes['baseline_elf'] == base.sha(args.baseline_elf.read_bytes())
    report = {'status': 'passed', 'upstream_commit': PIN, **hashes,
        'source_sha256': source_hashes, 'scene_vtable': scene_slots,
        'supported_direct_route': route, 'baseline_negative_controls': negatives,
        'leaf_contract': contract, 'caller_cases': callers,
        'scope': 'Host ROM/ARM11 CPU instructions; original DS divider register model; '
                 'real non-null input and input-enabled direction repeat; '
                 'stops before state-object operations. No game frame, Azahar, physical console or FPS proof.'}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({'status': report['status'], 'leaf_cases': contract['cases'],
                      'caller_cases': len(callers), 'baseline_faults': len(negatives),
                      'report': str(args.report)}))


if __name__ == '__main__':
    main()
