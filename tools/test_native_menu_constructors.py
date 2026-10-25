#!/usr/bin/env python3
"""Compare bounded DSMT constructor instruction paths against ASMP0 ROM.

Runs the actual native ELF and original ROM callers/constructors/parsers.
Controlled allocation, division, normalization and firmware seams isolate
constructor layouts and argument delivery from OS and previously audited math.
Does not launch Azahar, execute game frames or claim physical performance.
"""
import argparse
import json
import math
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import test_native_menu_callbacks as base
import ndspy.lz10
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
    UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR,
    UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC)

HEAP, LIMIT, CONTEXT = 0x12000000, 0x12100000, 0x12200000
STOP, STACK = base.STOP, base.STACK
SAVED = base.SAVED


def original_symbols():
    names = {}
    for file in (ROOT/'upstream/config/arm9/symbols.txt',
                 ROOT/'upstream/config/arm9/overlays/ov007/symbols.txt'):
        for line in file.read_text().splitlines():
            match = re.match(r'(\S+) kind:\S+ addr:0x([0-9a-f]+)', line)
            if match:
                names[match[1]] = int(match[2], 16)
    return names


def signed(value):
    return value-(1 << 32) if value & (1 << 31) else value


def quotient(a, b):
    assert b
    return (abs(a)//abs(b))*(-1 if (a < 0) != (b < 0) else 1)


class Probe:
    def __init__(self, cpu, names, label):
        self.cpu, self.names, self.label = cpu, names, label
        self.allocations, self.observations = [], []
        self.next = HEAP
        self.language = 0
        self.states, self.fault_events = [], []
        self.corrupt_fifth_texture_argument = False
        self.seams = {
            names['func_ov007_020c3df4']: self.allocate,
            names['_ZN4cstd3divEii']: self.divide,
            names['_ZN4cstd4ldivEii']: self.divide64,
            names['NormalizeVec3']: self.normalize,
            names['func_02059eb0']: self.settings,
        }
        self.cpu.hook_add(UC_HOOK_CODE, self.hook)

    def reg(self, i):
        return self.cpu.reg_read((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)[i])

    def u32(self, address):
        return struct.unpack('<I', self.cpu.mem_read(address, 4))[0]

    def hook(self, cpu, address, size, _):
        if address == self.names['func_ov007_020aea6c']:
            self.states.append((self.reg(0), self.next+32))
        if (self.corrupt_fifth_texture_argument and not self.fault_events and
                address == self.names['func_ov007_020c162c']):
            stack = cpu.reg_read(UC_ARM_REG_SP)
            value = self.u32(stack)
            cpu.mem_write(stack, struct.pack('<I', value ^ 4))
            self.fault_events.append({'argument': 5, 'from': hex(value), 'to': hex(value ^ 4)})
        if function := self.seams.get(address):
            function()
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

    def allocate(self):
        size = self.reg(1)
        assert size <= 0x20000, ('Invalid requested size', self.label, size)
        pointer = self.next+32
        self.next = (pointer+max(size, 1)+32+31) & ~31
        assert self.next < LIMIT
        self.cpu.mem_write(pointer-32, bytes([0xa5])*(size+64))
        self.allocations.append((pointer, size))
        self.cpu.reg_write(UC_ARM_REG_R0, pointer)

    def divide(self):
        args = [signed(self.reg(0)), signed(self.reg(1))]
        self.observations.append(('integer_division', args))
        self.cpu.reg_write(UC_ARM_REG_R0, quotient(*args) & 0xffffffff)

    def divide64(self):
        args = [signed(self.reg(0)), signed(self.reg(1))]
        self.observations.append(('fixed_division', args))
        result = quotient(args[0] << 32, args[1]) & 0xffffffffffffffff
        self.cpu.reg_write(UC_ARM_REG_R0, result & 0xffffffff)
        self.cpu.reg_write(UC_ARM_REG_R1, result >> 32)

    def normalize(self):
        source, destination = self.reg(0), self.reg(1)
        values = struct.unpack('<3i', self.cpu.mem_read(source, 12))
        # The original light constructor's real input is a cardinal vector.
        # No claim about arbitrary-vector rounding follows from this seam.
        assert sum(value != 0 for value in values) == 1, values
        magnitude = math.isqrt(sum(value*value for value in values))
        result = [quotient(value*4096, magnitude) for value in values]
        self.observations.append(('normalize_cardinal_vector', list(values)))
        self.cpu.mem_write(destination, struct.pack('<3i', *result))

    def settings(self):
        self.cpu.mem_write(self.reg(0), bytes([self.language])+bytes(0x53))
        self.observations.append(('firmware_language', self.language))

    def reset(self, language=0):
        self.next, self.language = HEAP, language
        self.allocations, self.observations = [], []
        self.states, self.fault_events = [], []
        self.cpu.mem_write(CONTEXT, bytes([0xa5])*0x188)
        self.cpu.mem_write(self.names['data_ov007_0210342c'], struct.pack('<I', CONTEXT))

    def call(self, name, args):
        # Five/six-argument boundaries run inside the original callers; this
        # entry helper only initializes up to four explicit register arguments.
        assert len(args) <= 4, (name, 'entry helper has no stack arguments')
        self.cpu.reg_write(UC_ARM_REG_CPSR, 0x10)
        for i, value in enumerate((*args, 0, 0, 0, 0)[:4]):
            self.cpu.reg_write((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)[i], value)
        self.cpu.reg_write(UC_ARM_REG_SP, STACK+0xf000)
        self.cpu.reg_write(UC_ARM_REG_LR, STOP)
        for i, reg in enumerate(SAVED):
            self.cpu.reg_write(reg, 0x12340000+i)
        self.cpu.emu_start(self.names[name], STOP, count=1000000)
        assert self.cpu.reg_read(UC_ARM_REG_PC) == STOP, ('Constructor did not return', self.label, name)
        assert self.cpu.reg_read(UC_ARM_REG_SP) == STACK+0xf000
        assert all(self.cpu.reg_read(reg) == 0x12340000+i for i, reg in enumerate(SAVED))
        blocks = []
        for pointer, size in self.allocations:
            assert bytes(self.cpu.mem_read(pointer-32, 32)) == bytes([0xa5])*32
            assert bytes(self.cpu.mem_read(pointer+size, 32)) == bytes([0xa5])*32
            blocks.append(bytes(self.cpu.mem_read(pointer, size)))
        return {'return': self.reg(0), 'allocations': list(self.allocations),
                'blocks': blocks, 'context': bytes(self.cpu.mem_read(CONTEXT, 0x188)),
                'observations': list(self.observations)}


def differences(original, native, pointer_field=None, compare_return=True):
    errors = []
    if compare_return and original['return'] != native['return']:
        errors.append({'kind': 'return', 'rom': hex(original['return']), 'native': hex(native['return'])})
    for key in ('allocations', 'observations', 'context'):
        if original[key] != native[key]:
            errors.append({'kind': key})
    for i, (left, right) in enumerate(zip(original['blocks'], native['blocks'])):
        if pointer_field and original['allocations'][i][0] == original['return']:
            offset, rom_value, native_value = pointer_field
            assert struct.unpack_from('<I', left, offset)[0] == rom_value
            assert struct.unpack_from('<I', right, offset)[0] == native_value
            left = left[:offset]+bytes(4)+left[offset+4:]
            right = right[:offset]+bytes(4)+right[offset+4:]
        if left != right:
            offsets = [j for j, pair in enumerate(zip(left, right)) if pair[0] != pair[1]]
            errors.append({'kind': 'allocation_bytes', 'index': i,
                           'offsets': offsets, 'rom_sha256': base.sha(left), 'native_sha256': base.sha(right)})
    assert len(original['blocks']) == len(native['blocks'])
    return errors


MODEL = 0x12300000


class Harness:
    def __init__(self, elf):
        self.cart = base.Cartridge(ROOT/'build/europe.pak')
        self.machine = base.Machine(elf, self.cart)
        base.verify_publication(self.machine)
        self.native = Probe(self.machine.cpu, self.machine.names, 'ARM11 ELF')
        cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        cpu.reg_write(UC_ARM_REG_C1_C0_2, 0xf << 20)
        cpu.reg_write(UC_ARM_REG_FPEXC, 1 << 30)
        cpu.mem_map(0x02000000, 0x120000)
        for module in (0xffffffff, 7):
            address, _, image = self.cart.image(module)
            cpu.mem_write(address, image)
        cpu.mem_map(base.ARENA, base.ARENA_SIZE)
        cpu.mem_map(base.STACK, 0x20000)
        self.original = Probe(cpu, original_symbols(), 'ASMP0 ROM')
        self.results = []
        _, indices = self.cart.handles()
        self.handle = self.original.u32(self.original.names['data_ov007_020d7584']+0x16*2) & 0xffff
        entry = self.cart.entries[indices[self.handle]]
        assert self.handle == 631 and entry[4] == 'data/DSMT/EUR/texBank_E.bmd'
        packed = self.cart.file(entry[4])
        assert packed[:5] == b'LZ77\x10'
        self.model = ndspy.lz10.decompress(packed[4:])
        assert len(self.model) == int.from_bytes(packed[5:8], 'little')
        assert struct.unpack_from('<I', self.model, 0x14)[0] == 11
        self.asset = {'handle': self.handle, 'path': entry[4], 'packed_size': len(packed),
                      'packed_sha256': base.sha(packed), 'unpacked_size': len(self.model),
                      'unpacked_sha256': base.sha(self.model)}
        for probe in (self.original, self.native):
            probe.seams[probe.names['LoadFile']] = lambda p=probe: self.load(p)

    def load(self, probe):
        assert probe.reg(0) == self.handle
        probe.observations.append(('real_menu_bmd_handle', self.handle))
        probe.cpu.reg_write(UC_ARM_REG_R0, MODEL)

    def run(self, function, rom_args, native_args=None, language=0,
            prepare_model=False, prepare_chain=(), observable_return=True,
            normalize=None, negative=False):
        snapshots = []
        for probe, values in ((self.original, rom_args),
                              (self.native, rom_args if native_args is None else native_args)):
            probe.reset(language)
            probe.corrupt_fifth_texture_argument = negative and probe is self.native
            if prepare_model:
                probe.cpu.mem_write(MODEL, self.model)
                probe.cpu.mem_write(probe.names['data_ov007_02103370'], bytes(0x2e*4))
                for slot, callback in (('data_ov007_02103368', 'func_ov007_020cca74'),
                                       ('data_ov007_0210336c', 'func_ov007_020cca68')):
                    probe.cpu.mem_write(probe.names[slot], struct.pack('<I', probe.names[callback]))
            for constructor in prepare_chain:
                probe.call(constructor, [])
            snapshots.append(probe.call(function, values))
        left, right = snapshots
        checked_fields = []

        def field(label, rom_pointer, native_pointer, offset, rom_value, native_value):
            assert rom_pointer == native_pointer, (label, 'controlled allocation identity')
            for snapshot, pointer, value in ((left,rom_pointer,rom_value),
                                              (right,native_pointer,native_value)):
                index = [p for p,_ in snapshot['allocations']].index(pointer)
                block = bytearray(snapshot['blocks'][index])
                actual = struct.unpack_from('<I', block, offset)[0]
                assert actual == value, (label, offset, hex(actual), hex(value))
                block[offset:offset+4] = bytes(4)
                snapshot['blocks'][index] = bytes(block)
            checked_fields.append({'consumer': label, 'offset': hex(offset),
                                   'rom_value': hex(rom_value), 'native_value': hex(native_value)})

        if normalize:
            normalize(field)
        errors = differences(left, right, compare_return=observable_return)
        model_hash = None
        if prepare_model:
            relocated = bytes(self.original.cpu.mem_read(MODEL, len(self.model)))
            native_model = bytes(self.native.cpu.mem_read(MODEL, len(self.model)))
            if relocated != native_model:
                errors.append({'kind': 'relocated_bmd_file_bytes',
                               'offsets': [i for i,(a,b) in enumerate(zip(relocated,native_model)) if a != b]})
            model_hash = base.sha(relocated)
            for probe in (self.original, self.native):
                assert probe.u32(probe.names['data_ov007_02103370']+0x16*4) == MODEL
        item = {'function': function, 'rom_arguments': [hex(a) for a in rom_args],
                'native_arguments': [hex(a) for a in (rom_args if native_args is None else native_args)],
                'firmware_language': language, 'return_is_observable': observable_return,
                'prepared_constructors': list(prepare_chain),
                'controlled_calls': left['observations'], 'allocation_count': len(left['allocations']),
                'allocation_sizes': [size for _,size in left['allocations']],
                'normalized_allocation_sha256': base.sha(b''.join(left['blocks'])),
                'checked_borrowed_fields': checked_fields, 'relocated_bmd_sha256': model_hash,
                'differences': errors}
        if negative:
            assert len(self.native.fault_events) == 1
            assert any(error['kind'] == 'allocation_bytes' for error in errors), errors
            item['status'] = 'expected ABI corruption detected'
            item['injected_fault'] = self.native.fault_events[0]
            return item
        assert not errors, item
        self.results.append(item)
        return item

    def normalize_sprite_templates(self, firmware_language, field):
        original, native = self.original, self.native
        assert [state for state,_ in original.states] == list(range(24))
        assert [state for state,_ in native.states] == list(range(24))
        language = {0:1, 1:1, 2:2, 3:3, 4:5, 5:4, 6:0}[firmware_language]
        for state in range(24):
            mode = original.u32(original.names['data_ov007_020d77d8']+state*12)
            templates = [('data_ov007_020d76e8', state*10),
                         ('data_ov007_020d78f8', mode*20)]
            if language == 5 and state <= 2:
                templates = [('data_ov007_020d7680', state*10),
                             ('data_ov007_020d76c0', (mode-1)*20)]
            if state == 8 and language in (3,4):
                templates[1] = ('data_ov007_020d7658' if language == 3
                                 else 'data_ov007_020d766c', 0)
            for offset, (name, addend) in zip((0x24,0x28), templates):
                field('sprite state '+str(state), original.states[state][1], native.states[state][1],
                      offset, original.names[name]+addend, native.names[name]+addend)

    def normalize_2d_graph(self, field):
        original, native = self.original, self.native
        rom_graph, native_graph = original.u32(CONTEXT+0x74),native.u32(CONTEXT+0x74)
        assert rom_graph == native_graph
        for kind, member, table, count, prefix in (
                ('pixel',0x24,'data_ov007_020d7e28',4,0x24),
                ('palette',0x2c,'data_ov007_020d7e38',6,0x20)):
            rom_list, native_list = original.u32(rom_graph+member),native.u32(native_graph+member)
            for index in range(count):
                field(kind+' '+str(index),original.u32(rom_list+4*index),native.u32(native_list+4*index),0,
                      original.u32(original.names[table]+4*index)+prefix,
                      native.u32(native.names[table]+4*index)+prefix)
        rom_list, native_list = original.u32(rom_graph+0x34),native.u32(native_graph+0x34)
        for index in range(8):
            rom_object, native_object = original.u32(rom_list+4*index),native.u32(native_list+4*index)
            rom_resource = original.u32(original.names['data_ov007_020d7ec8']+4*index)
            native_resource = native.u32(native.names['data_ov007_020d7ec8']+4*index)
            if index in (6,7):
                source = 'data_ov007_02104598' if index == 6 else 'data_ov007_02103f98'
                field('background '+str(index),rom_object,native_object,0x10,
                      original.names[source],native.names[source])
                field('background '+str(index),rom_object,native_object,0x14,
                      original.names['data_ov007_02103998'],native.names['data_ov007_02103998'])
            elif index == 1:
                field('background 1 preserved tilemap',rom_object,native_object,0x14,
                      rom_resource+0x28,native_resource+0x28)
            elif index != 5 and rom_resource:
                field('background '+str(index),rom_object,native_object,0x10,
                      rom_resource+0x28,native_resource+0x28)
        for state in range(9):
            rom_slot, native_slot = original.u32(CONTEXT+0xa4+state*4),native.u32(CONTEXT+0xa4+state*4)
            rom_object, native_object = original.u32(rom_slot),native.u32(native_slot)
            field('2D state '+str(state),rom_object,native_object,0x24,
                  original.names['data_ov007_020ccbc4']+state*10,
                  native.names['data_ov007_020ccbc4']+state*10)
            assert original.u32(rom_object+0x28) == native.u32(native_object+0x28) == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, default=ROOT/'build/3ds/source/ctr/sm64ds-native.elf')
    parser.add_argument('--report', type=Path, default=ROOT/'build/native-runtime/menu-constructors-test.json')
    args = parser.parse_args()
    elf_sha256 = base.sha(args.elf.read_bytes())
    harness = Harness(args.elf)
    original, native = harness.original, harness.native
    for language in range(7):
        harness.run('func_ov007_020b6a84', [], language=language, observable_return=False)
    for function, table, count, output_field, prefix in (
            ('func_ov007_020c99d8','data_ov007_020d7e28',4,0,0x24),
            ('func_ov007_020c9988','data_ov007_020d7e38',6,0,0x20),
            ('func_ov007_020c9688','data_ov007_020d7e50',6,None,None),
            ('func_ov007_020c95fc','data_ov007_020d7ec8',8,0x10,0x28)):
        for index in range(count):
            rom_value = original.u32(original.names[table]+4*index)
            native_value = native.u32(native.names[table]+4*index)
            assert bool(rom_value) == bool(native_value)
            rom_args, native_args = [rom_value], [native_value]
            if function.endswith('020c95fc'):
                attributes = bytes(original.cpu.mem_read(original.names['data_ov007_020d7a24']+index*2,2))
                assert attributes == bytes(native.cpu.mem_read(native.names['data_ov007_020d7a24']+index*2,2))
                rom_args.extend(attributes)
                native_args.extend(attributes)
            def normalize(field, rom_value=rom_value, native_value=native_value,
                          output_field=output_field, prefix=prefix, function=function, index=index):
                field(function+' resource '+str(index),original.reg(0),native.reg(0),output_field,
                      rom_value+prefix,native_value+prefix)
            harness.run(function,rom_args,native_args,
                        normalize=normalize if output_field is not None and rom_value else None)
    for function in ('func_ov007_020c99d8', 'func_ov007_020c9988'):
        harness.run(function,[0])
    harness.run('func_ov007_020b7594',[0x1f,0x1f])
    harness.run('func_ov007_020b69c4',[],observable_return=False)
    harness.run('func_ov007_020b697c',[],prepare_model=True,observable_return=False)
    for count in (0,1,4,8):
        harness.run('func_ov007_020c11ac',[count])
    for language in range(7):
        harness.run('func_ov007_020b693c',[],language=language,prepare_model=True,
                    prepare_chain=('func_ov007_020b6a84','func_ov007_020b697c','func_ov007_020b69c4'),
                    observable_return=False,
                    normalize=lambda field,language=language: harness.normalize_sprite_templates(language,field))
    harness.run('func_ov007_020b6d40',[],observable_return=False,normalize=harness.normalize_2d_graph)
    negative = harness.run('func_ov007_020b697c',[],prepare_model=True,
                           observable_return=False,negative=True)
    assert base.sha(args.elf.read_bytes()) == elf_sha256, 'ELF changed during the comparison'
    result = {'status': 'passed',
              'scope': 'Bounded ROM/production ARM11 constructor instruction/data/ABI regression on host; no complete menu frame, Azahar, physical console or performance proof',
              'elf': str(args.elf), 'elf_sha256': elf_sha256,
              'upstream': '6230b2996636e3fd0e03cb178161c94cb4b73cc9',
              'asset': harness.asset, 'comparisons': len(harness.results),
              'sprite_states_checked': 168, 'results': harness.results,
              'negative_control': negative,
              'seams': ['controlled allocation with A5 canaries',
                        'integer/fixed division observations', 'cardinal-vector normalization observation',
                        'firmware-language observation', 'verified/decompressed BMD bytes at the real handle',
                        'native startup image/catalog/C++ heap seams from callback regression'],
              'pointer_normalization': 'Only explicit borrowed fields with exact symbol/addend or resource-prefix assertions; no general pointer/range heuristic',
              'cartridge_bytes_written_to_disk': False}
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'status': 'passed', 'report': str(args.report),
                      'comparisons': result['comparisons'], 'sprite_states': result['sprite_states_checked'],
                      'negative_control': negative['status']},indent=2))


if __name__ == '__main__':
    main()

