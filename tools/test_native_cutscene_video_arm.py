#!/usr/bin/env python3
"""Compare original OV002 ARM9 and the production ARM11 cutscene controls.

The real ROM control bodies, counters, RNG, nested helpers and blend/scroll
SDK code execute. OAM enqueue, the active-scene query and func_0201ef50 are
declared external boundaries. All fixture bytes, globals and video registers
are compared. Native DS MMIO remains unmapped. No rendered frame, integrated
scene readiness, physical hardware or FPS proof is produced by this gate.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess

import test_native_audio_deferred_arm as shared
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_LR, UC_ARM_REG_SP

ROOT, WORK, base = shared.ROOT, shared.WORK, shared.base
SELF, STATE, SCENE, VTABLE, QUERY = (WORK+0x100, WORK+0x1100,
    WORK+0xf000, WORK+0xf100, WORK+0xf800)
GLOBALS = [('data_0209d454', 'B', 0x1f), ('data_020a0db0', 'I', 0),
           ('data_0209e650', 'I', 0x12345678),
           ('data_ov002_02111144', 'B', 0)]
SUFFIXES = ['1fcc', '23f0', '2aec', '2bf4', '30f8', '335c', '340c',
            '34bc', '35e4', '3828', '5b24']


def original_machine(cart):
    p = shared.Probe('original ASMP0 ARM9', cart=cart)
    address, _, image = cart.image(2)
    p.cpu.mem_write(address, image)
    metadata = (ROOT/'upstream/config/arm9/overlays/ov002/symbols.txt').read_text()
    p.names.update({n: int(a, 16) for n, a in
        re.findall(r'(\S+) kind:\S+ addr:0x([0-9a-f]+)', metadata)})
    p.cpu.mem_map(0x04000000, 0x2000)
    return p


def fixture(case):
    raw = bytearray(b'\xa5'*0x10000)
    raw[SELF-WORK:SELF-WORK+0x800] = bytes(0x800)
    raw[STATE-WORK:STATE-WORK+0x100] = bytes(0x100)
    def put(address, fmt, *values):
        struct.pack_into('<'+fmt, raw, address-WORK, *values)
    put(SELF+0xd4, 'I', STATE)
    put(SELF+0x100, 'B', case.get('animation', 0))
    put(STATE+0x34, 'B', case.get('state', 0))
    put(STATE, 'hhhhHH', -20, 20, 100, case.get('scroll', 0),
        case.get('alpha', 3), 13)
    put(STATE+0x24, 'HHH', 2, 3, 4)
    put(STATE+0x2c, 'HH', case.get('timer', 0), case.get('fade', 2))
    for j in range(3):
        put(STATE+0xc+j*2, 'h', [-63, 0, 319][j])
        put(STATE+0x12+j*2, 'h', 20+j*40)
        put(STATE+0x18+j*2, 'h', [-1, 1, 1][j])
        put(STATE+0x30+j, 'B', 1)
    for i in range(2):
        at = SELF+i*0x30
        put(at+0x130, 'ii', *case.get('offsets', (-0x60000, -0x38000)))
        put(at+0x140, 'iiii', *case.get('window', (0x81000, 0x55000, 0xfb000, 0xbb000)))
        put(at+0x150, 'iiH', *case.get('speed', (0x1800, 0x1800)), case.get('delay', 0))
        put(at+0x15c, 'BBB', 3, 7, 2)
        put(SELF+0x1c4+i*8, 'HH', case.get('counter', 0), case.get('subcounter', 0))
        put(SELF+0x1c9+i*8, 'BBB', 2, case.get('blend', 0), 1)
    for j in range(5):
        put(SELF+0x160+j*0x14, 'ii', 0x4000+j*4096, -0x5000-j*4096)
        put(SELF+0x170+j*0x14, 'BBB', j%2, 9, 2)
    put(SCENE, 'I', VTABLE)
    put(VTABLE+20, 'I', QUERY)
    # Active-scene query is explicitly a service boundary, never game logic.
    put(QUERY, 'I', 0xe12fff1e)
    return bytes(raw)


def execute(p, raw, case, registers, native=False):
    p.cpu.mem_write(WORK, raw)
    for name, fmt, value in GLOBALS:
        if name == 'data_020a0db0': value = case.get('pause', 0)
        p.cpu.mem_write(p.names[name], struct.pack('<'+fmt, value))
    p.words(p.names['data_0209f5bc'], SCENE)
    spans = {}
    if native:
        assert not any(b <= 0x04000000 <= e for b, e, _ in p.cpu.mem_regions())
        p.call('_ZN6sm64ds18native_video_resetEv', [], instruction_limit=1000000)
        for label, address, size in [('registers', 0x04000000, 0x2000),
            ('palette', 0x05000000, 0x800), ('vram', 0x06800000, 0xa4000),
            ('oam', 0x07000000, 0x800)]:
            pointer = p.call('_ZN6sm64ds20native_video_pointerEmj', [address, size])
            value = registers if label == 'registers' else b'\xa5'*size
            p.cpu.mem_write(pointer, value)
            spans[label] = (pointer, value)
        reg = spans['registers'][0]
    else:
        reg = 0x04000000
        p.cpu.mem_write(reg, registers)
    events = []
    boundaries = {p.names[n]: n for n in
        ['_ZN3OAM9RenderSubEP7OamAttrii', '_ZN3OAM9RenderSubEP7OamAttriiii', 'func_0201ef50']}
    def code(cpu, address, size, _):
        if address == QUERY:
            events.append({'boundary': 'scene_active', 'return': case.get('active', 1)})
            cpu.reg_write(shared.REGS[0], case.get('active', 1))
        elif address in boundaries:
            name = boundaries[address]
            count = 1 if name == 'func_0201ef50' else (4 if name.endswith('iiii') else 3)
            args = [cpu.reg_read(r) for r in shared.REGS[:count]]
            if name.endswith('iiii'):
                args += [p.u32(cpu.reg_read(UC_ARM_REG_SP))]
            if name.startswith('_ZN3OAM'):
                args[0] = base.sha(bytes(cpu.mem_read(args[0], 8)))
            events.append({'boundary': name, 'arguments': args})
        else: return
        cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
    hook = p.cpu.hook_add(UC_HOOK_CODE, code)
    name = 'func_ov002_020f'+case['suffix']
    args = [SELF] + ([case.get('index', 0)] if case['suffix'] in
        ['2aec', '2bf4', '30f8', '335c', '340c', '34bc', '35e4'] else [])
    result = p.call(name, args)
    p.cpu.hook_del(hook)
    state = bytes(p.cpu.mem_read(WORK, 0x10000))
    # All bytes outside both owned objects (including query/vtable) are guards.
    for start, end in [(0, SELF-WORK), (SELF-WORK+0x800, STATE-WORK),
                       (STATE-WORK+0x100, 0x10000)]:
        assert state[start:end] == raw[start:end], (name, 'fixture guard')
    globals_after = {n: bytes(p.cpu.mem_read(p.names[n], struct.calcsize('<'+f))).hex()
                     for n, f, _ in GLOBALS}
    if native:
        assert p.call('_ZN6sm64ds15native_video_okEv', []) == 1
        for label, (pointer, before) in spans.items():
            if label != 'registers':
                assert bytes(p.cpu.mem_read(pointer, len(before))) == before, (name, label)
    return state, bytes(p.cpu.mem_read(reg, 0x2000)), globals_after, events, result if case['suffix']=='23f0' else None


def cases():
    rows = []
    def add(suffix, label, **kw): rows.append(dict(suffix=suffix, name=label, **kw))
    for t, f in [(0,0), (0,2), (22,2), (23,2), (23,1), (0xffff,1)]:
        add('1fcc', f'fade-{t}-{f}', timer=t, fade=f)
    for state in range(7):
        for pause in [0,1]:
            add('23f0', f'dispatch-{state}-pause-{pause}', state=state, pause=pause,
                scroll={2:127,5:223}.get(state,40), timer=23, fade=1,
                animation={1:3,4:4}.get(state,0), alpha=7)
    add('23f0','scene-inactive',active=0)
    for suffix in ['2aec','2bf4']:
        for idx in [0,1]:
            for counter,subcounter in [(0,0),(0,1),(28,0),(29,1)]:
                add(suffix,f'{suffix}-{idx}-{counter}-{subcounter}',index=idx,
                    counter=counter,subcounter=subcounter,blend=14 if counter else 0)
    for idx in [0,1]:
        add('30f8',f'scroll-delay-{idx}',index=idx,delay=2)
        add('30f8',f'scroll-normal-{idx}',index=idx)
        add('30f8',f'scroll-all-clamps-{idx}',index=idx,
            offsets=(-0x100, -0x100),window=(0x100,0x100,0xfef00,0xbff00))
        for suffix in ['335c','340c']:
            for y in [0x50000,0x64800,0x66000]:
                add(suffix,f'{suffix}-{idx}-{y}',index=idx,window=(0x80000,y,0xfc000,0xbc000))
        for window in [(0x82000,0x58000,0xfa000,0xba000),(0x81000,0x57000,0xfb000,0xbb000)]:
            add('34bc',f'expand-{idx}-{window[0]}',index=idx,window=window)
        add('35e4',f'window-init-{idx}',index=idx)
    add('3828','second-window-init')
    add('5b24','window-cleanup')
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, default=ROOT/'build/3ds/source/ctr/sm64ds-native.elf')
    parser.add_argument('--report', type=Path, default=ROOT/'build/native-integration-check/cutscene-video-arm-gate.json')
    args = parser.parse_args()
    assert not args.report.exists(), 'Preserve the existing closed gate; choose a new report path'
    assert subprocess.check_output(['git','-C',ROOT/'upstream','rev-parse','HEAD'],text=True).strip()==shared.PIN
    cart = base.Cartridge(ROOT/'build/europe.pak')
    rom, native = original_machine(cart), shared.Probe('production ARM11', elf=args.elf)
    # The integration loader populates these BSS-backed original data views.
    # This isolated gate supplies their genuine ROM bytes and relocates only
    # the three sprite pointers; it does not assert loader/Stage readiness.
    attr = bytes(rom.cpu.mem_read(rom.names['data_ov002_0210bddc'], 64))
    native.cpu.mem_write(native.names['data_ov002_0210bddc'], attr)
    sprites = [bytes(rom.cpu.mem_read(rom.u32(rom.names['data_ov002_0210be1c']+i*4), 8))
               for i in range(3)]
    native.words(native.names['data_ov002_0210be1c'], *(WORK+0xe000+i*0x100 for i in range(3)))
    registers = bytes((i*7+3)&255 for i in range(0x2000))
    results=[]
    for case in cases():
        raw=bytearray(fixture(case))
        for i, sprite in enumerate(sprites): raw[0xe000+i*0x100:0xe008+i*0x100]=sprite
        raw=bytes(raw)
        expected=execute(rom,raw,case,registers)
        actual=execute(native,raw,case,registers,True)
        assert actual==expected, (case, [(i, base.sha(a) if isinstance(a,bytes) else a,
            base.sha(b) if isinstance(b,bytes) else b) for i,(a,b) in enumerate(zip(expected,actual)) if a!=b])
        results.append(dict(case=case, fixture_sha256=base.sha(raw),
            state_sha256=base.sha(actual[0]), registers_sha256=base.sha(actual[1]),
            globals=actual[2], boundary_events=actual[3], return_value=actual[4],
            full_state_registers_and_guards_compared=True, ABI_preserved=True))
    report=dict(status='passed', ELF_sha256=base.sha(args.elf.read_bytes()),
        upstream_commit=shared.PIN, cases=results, functions=len(SUFFIXES),
        ROM_data_fixtures={'OAM_attribute_sha256':base.sha(attr),
            'sprite_sha256':[base.sha(s) for s in sprites], 'three_sprite_pointer_relocations':True},
        DS_MMIO_unmapped_on_native=True,
        declared_boundaries=['OAM enqueue','active-scene query','func_0201ef50 UI service'],
        setup_resource_uploads='func_ov002_020f5fe4 requires a separate resource gate',
        limitations=['no integrated scene execution','no rendered frame','no physical hardware or FPS'])
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(status=report['status'],functions=len(SUFFIXES),cases=len(results),report=str(args.report))))


if __name__=='__main__': main()
