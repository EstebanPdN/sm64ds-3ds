#!/usr/bin/env python3
"""Compare complete cuboid/cylinder shadow loading and lifecycle with ASMP0.

The immutable production ELF performs its actual ROM data publication. Original
ASMP0 instruction bodies use the cartridge images and declared zeroed ARM9 BSS.
Bounded component allocations, allocation failures and a separate call stack are
fixtures. GX palette Begin/End are observed device boundaries: both templates
have zero textures/palettes, so no upload is replaced. This checks model parsing,
bone transforms, materials, caching and list cleanup, not GPU/game/hardware FPS.
No captured RAM is imported into another ELF. Cartridge bytes stay in memory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from test_native_menu_callbacks import Machine, Cartridge, verify_publication, SAVED
from test_native_audio_deferred_arm import Probe, WORK, STACK, STOP
from test_native_overlay_static_init_arm import original_names
from analyze_native_acquisition import elf_bytes
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R3, UC_ARM_REG_PC, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_CPSR)

ROOT = Path(__file__).resolve().parents[1]
PIN = '6230b2996636e3fd0e03cb178161c94cb4b73cc9'
REGS = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)
OBJECTS = (WORK+0x1000, WORK+0x1100)
MATRICES = (WORK+0x2000, WORK+0x2100)
INIT = '_ZN11ShadowModel9InitModelEP9Matrix4x35Fix12IiES3_S3_j'
CLEAN = '_ZN11ShadowModel8CleanAllEv'
DTOR = '_ZN11ShadowModelD1Ev'
GX = ('_ZN2GX16BeginLoadTexPlttEv', '_ZN2GX14EndLoadTexPlttEv')
PIPELINE = ('_ZN11ShadowModel9DoSetFileEPcii', 'func_02016e70',
    '_ZN5Model23AddToCommonModelDataArrER8BMD_File',
    '_ZN5Model13LoadTexAndPalER8BMD_File', 'func_02046564',
    'func_020462d0', '_ZN15ModelComponents21UpdateVertsUsingBonesEv',
    'Matrix4x3_LoadIdentity', 'func_02045074')

def sha(data): return hashlib.sha256(data).hexdigest()

def words(cpu, address, count):
    return list(struct.unpack('<'+'I'*count, cpu.mem_read(address, count*4)))

def put(cpu, address, *values):
    cpu.mem_write(address, struct.pack('<'+'I'*len(values),
        *(v & 0xffffffff for v in values)))

def call(probe, name, arguments=()):
    cpu = probe.cpu
    cpu.reg_write(UC_ARM_REG_CPSR, 0x10)
    sp = STACK+0xf000
    cpu.mem_write(sp-0x2000, bytes([0x6d])*0x3000)
    for reg, value in zip(REGS, (*arguments, 0, 0, 0, 0)[:4]):
        cpu.reg_write(reg, value & 0xffffffff)
    for i, value in enumerate(arguments[4:]): put(cpu, sp+4*i, value)
    incoming = bytes(cpu.mem_read(sp, 64))
    for i, reg in enumerate(SAVED): cpu.reg_write(reg, 0x12340000+i)
    cpu.reg_write(UC_ARM_REG_SP, sp); cpu.reg_write(UC_ARM_REG_LR, STOP)
    probe.active = True
    try: cpu.emu_start(probe.names[name], STOP, count=100000)
    finally: probe.active = False
    assert cpu.reg_read(UC_ARM_REG_PC) == STOP, (probe.label, name, 'instruction bound')
    assert cpu.reg_read(UC_ARM_REG_SP) == sp
    assert [cpu.reg_read(r) for r in SAVED] == [0x12340000+i for i in range(8)]
    assert bytes(cpu.mem_read(sp, 64)) == incoming
    assert bytes(cpu.mem_read(sp-0x2000, 32)) == bytes([0x6d])*32
    return cpu.reg_read(UC_ARM_REG_R0)

class Observer:
    def __init__(self, probe, native):
        self.p, self.native = probe, native
        self.instructions = 0
        self.p.active = False
        names = probe.names
        self.allocators = {names['sm64ds_game_operator_new' if native else '_Znwj']:'model',
            names['_ZN6Memory13operator_new2Ej']:'buffer'}
        self.delete = names['_ZN6Memory16operator_delete2EPv']
        self.device = {names[k]:k for k in GX}
        self.pipeline = {names[k]:k for k in PIPELINE}
        probe.cpu.hook_add(UC_HOOK_CODE, self.code)
        probe.cpu.hook_add(UC_HOOK_MEM_WRITE, self.write)

    def reset(self, fill, fail):
        p, cpu = self.p, self.p.cpu
        self.fill, self.fail, self.failed = fill, fail, False
        self.next = WORK+0x4000
        self.allocations, self.freed, self.calls, self.boundaries = [], [], [], []
        self.writes = 0
        cpu.mem_write(WORK, bytes([0x6b])*0x10000)
        for obj in OBJECTS: cpu.mem_write(obj, bytes([fill])*40)
        for mat in MATRICES:
            put(cpu, mat, 4096, 0, 0, 0, 4096, 0, 0, 0, 4096, 100, 200, 300)
        put(cpu, p.names['data_0209cef8'], 0)
        cpu.mem_write(p.names['data_0209cefc'], bytes(24))
        put(cpu, p.names['data_0209cef4'], 0)
        cpu.mem_write(p.names['data_0209ceec'], b'\0')
        # ASMP0 initializes this policy to one; the live STOP also had one.
        assert words(cpu, p.names['data_0208e738'], 1) == [1]
        self.allowed = [(obj, 40) for obj in OBJECTS]+[(mat, 48) for mat in MATRICES]
        self.allowed += [(p.names['data_0209cef8'], 4),
            (p.names['data_0209cefc'], 24), (p.names['data_0209cef4'], 4),
            (p.names['data_0209ceec'], 1), (STACK+0xd000, 0x2000)]

    def code(self, cpu, address, size, _):
        if not self.p.active: return
        self.instructions += 1
        if address == self.p.names.get('sm64ds_native_missing') or address == self.p.names['Crash']:
            raise AssertionError(('unexpected fatal boundary', self.p.label, hex(address)))
        if address in self.pipeline: self.calls.append(self.pipeline[address])
        if address in self.device:
            self.boundaries.append(self.device[address])
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
        elif address in self.allocators:
            kind, size = self.allocators[address], cpu.reg_read(UC_ARM_REG_R0)
            assert size in (20, 148), (kind, size)
            pointer = 0
            if kind == self.fail and not self.failed: self.failed = True
            else:
                pointer = self.next+32
                self.next = (pointer+size+32+15) & ~15
                assert self.next < WORK+0xf000
                cpu.mem_write(pointer, bytes([self.fill])*size)
                self.allowed.append((pointer, size))
            self.allocations.append((kind, size, pointer))
            cpu.reg_write(UC_ARM_REG_R0, pointer)
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
        elif address == self.delete:
            self.freed.append(cpu.reg_read(UC_ARM_REG_R0))
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

    def write(self, cpu, access, address, size, value, _):
        if not self.p.active: return
        assert any(start <= address and address+size <= start+length
            for start, length in self.allowed), ('write outside output/stack', self.p.label, hex(address), size)
        self.writes += 1

    def guards(self):
        for _, size, pointer in self.allocations:
            if pointer:
                assert bytes(self.p.cpu.mem_read(pointer-32, 32)) == bytes([0x6b])*32
                assert bytes(self.p.cpu.mem_read(pointer+size, 32)) == bytes([0x6b])*32
        for obj in OBJECTS:
            assert bytes(self.p.cpu.mem_read(obj-32, 32)) == bytes([0x6b])*32
            assert bytes(self.p.cpu.mem_read(obj+40, 32)) == bytes([0x6b])*32

def outcome(observer, template, fill, fail, normal):
    p, cpu = observer.p, observer.p.cpu
    observer.reset(fill, fail)
    address = p.names['data_020ad524' if template == 'Cuboid' else 'data_020ad560']
    before = bytes(cpu.mem_read(address, 60))
    header = words(cpu, address, 15)
    assert header[1] == header[9] == 1 and header[5] == header[7] == 0
    name = f'_ZN11ShadowModel{10 if template == "Cuboid" else 12}Init{template}Ev'
    for obj in OBJECTS: call(p, '_ZN11ShadowModelC1Ev', [obj])
    returned = [call(p, name, [OBJECTS[0]])]
    failures = []
    if fail:
        assert returned[-1] == 0
        failures.append({'record':[normal(x) for x in words(cpu, p.names['data_0209cefc'], 3)],
            'count':words(cpu, p.names['data_0209cef8'], 1)[0],
            'shadow_data':words(cpu, OBJECTS[0]+8, 1)[0]})
        returned.append(call(p, name, [OBJECTS[0]]))
    assert returned[-1] == 1
    data = words(cpu, OBJECTS[0]+8, 1)[0]
    component = [normal(x) for x in words(cpu, data, 5)]
    buffer = words(cpu, data+12, 1)[0]
    raw_buffer = bytes(cpu.mem_read(buffer, 148))
    flags = words(cpu, words(cpu, data+4, 1)[0]+0x24, 1)[0]
    assert flags & 0x8000
    assert words(cpu, buffer, 12) == [4096,0,0,0,4096,0,0,0,4096,0,0,0]
    alloc_before_cache = len(observer.allocations)
    assert call(p, name, [OBJECTS[1]]) == 1
    assert len(observer.allocations) == alloc_before_cache
    assert words(cpu, OBJECTS[1]+8, 1)[0] == data
    assert words(cpu, p.names['data_0209cef8'], 1) == [1]
    for i, obj in enumerate(OBJECTS): call(p, INIT, [obj, MATRICES[i], 4096, 3584, 4608, 0x76+i])
    assert words(cpu, p.names['data_0209cef4'], 1) == [OBJECTS[1]]
    linked = [[normal(x) for x in words(cpu, obj, 10)] for obj in OBJECTS]
    if fill == 0xff:
        call(p, CLEAN)
        assert words(cpu, p.names['data_0209cef4'], 1) == [0]
    for obj in reversed(OBJECTS): call(p, DTOR, [obj])
    destroyed = [[normal(x) for x in words(cpu, obj, 10)] for obj in OBJECTS]
    assert words(cpu, p.names['data_0209cef4'], 1) == [0]
    assert bytes(cpu.mem_read(address, 60)) == before
    observer.guards()
    assert observer.boundaries == list(GX)
    assert set(PIPELINE) <= set(observer.calls)
    return {'returns':returned, 'failure_states':failures, 'components':component,
        'buffer_sha256':sha(raw_buffer), 'material_flags':flags,
        'allocations':observer.allocations, 'released_failed_allocations':observer.freed,
        'linked':linked, 'destroyed':destroyed, 'device_boundaries':observer.boundaries,
        'pipeline':observer.calls}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, default=ROOT/'build/native-integration-check/candidate-shadow-cuboid/sm64ds-native.elf')
    parser.add_argument('--pack', type=Path, default=ROOT/'build/europe.pak')
    parser.add_argument('--report', type=Path, default=ROOT/'build/native-stage-plan/shadow-model-gate.json')
    args = parser.parse_args()
    assert subprocess.check_output(['git','-C',ROOT/'upstream','rev-parse','HEAD'], text=True).strip() == PIN
    cart = Cartridge(args.pack)
    native = Machine(args.elf, cart); native.label = 'immutable production ARM11'
    verify_publication(native)
    original = Probe('ASMP0 ROM', cart=cart); original.names = original_names(1)
    text = (ROOT/'upstream/config/arm9/delinks.txt').read_text()
    match = re.search(r'    \.bss\s+start:0x([0-9a-f]+) end:0x([0-9a-f]+)', text)
    assert match
    begin, end = [int(x,16) for x in match.groups()]
    original.cpu.mem_write(begin, bytes(end-begin))
    address, bss, image = cart.image(1)
    original.cpu.mem_write(address, image)
    original.cpu.mem_write(address+len(image), bytes(bss))
    native_raw = args.elf.read_bytes()
    start, end = [native.names[k] for k in ('sm64ds_runtime_sections','sm64ds_runtime_sections_end')]
    table = elf_bytes(native_raw, start, end-start)
    sections = [struct.unpack_from('<5I',table,i) for i in range(0,len(table),20)]
    def normal(value):
        for name in ('_ZTV11ShadowModel','_ZTV9ModelBase'):
            if value == original.names[name]: return native.names[name]+8
        for target, module, source, size, zero in sections:
            if module in (1,0xffffffff) and source <= value < source+size:
                return target+value-source
        return value
    observers = [Observer(original,False), Observer(native,True)]
    cases = []
    for template in ('Cuboid','Cylinder'):
        for fill, fail in [(0,None),(0xa5,None),(0xff,None),(0xa5,'model'),(0xa5,'buffer')]:
            results = [outcome(obs,template,fill,fail,normal if obs is observers[0] else lambda x:x)
                for obs in observers]
            assert results[0] == results[1], (template,fill,fail,results)
            cases.append({'template':template,'poison_byte':fill,'failed_allocation':fail,
                'status':'PASS','component_bytes':20,'buffer_bytes':148,
                'buffer_sha256':results[0]['buffer_sha256'], 'retry_after_failure':bool(fail),
                'cached_model_shared':True,'identity_transforms':True,'full_buffers_equal':True,
                'unlink_and_destruction':True,'CleanAll_executed':fill == 0xff,
                'material_flags':hex(results[0]['material_flags'])})
    report = {'status':'PASS','scope':__doc__,'ELF_sha256':sha(native_raw),
        'upstream_pin':PIN,'old_RAM_imported':False,'production_recompiled_for_gate':False,
        'cases':cases,'ROM_instructions':observers[0].instructions,
        'native_instructions':observers[1].instructions,'allocation_and_stack_canaries':True,
        'bounded_writes':True,'callee_saved_and_stack_ABI':True,
        'device_boundaries':list(GX),'GPU_verified':False,'game_frames_executed':False,
        'physical_hardware':False}
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'status':report['status'],'cases':len(cases),
        'ROM_instructions':report['ROM_instructions'],'native_instructions':report['native_instructions'],
        'report':str(args.report)}))

if __name__ == '__main__': main()
