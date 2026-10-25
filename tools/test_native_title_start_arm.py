#!/usr/bin/env python3
"""Compare the isolated title-start gate in ASMP0 ROM and an immutable ELF.

Runs original instructions with explicit state/alpha/input fixtures. The sole
observed animation seam records its arguments and returns without running the
animation. Neither game frames nor Azahar nor physical hardware are executed.
Also compiles and checks a test-only observer's ARM11 layout and guarded reads.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile

import test_native_audio_deferred_arm as shared
import ndspy.lz10
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_LR

ROOT, TOOL, PIN = shared.ROOT, shared.TOOL, shared.PIN
WORK = shared.WORK
CTX, SUB, F4, ANIM, ELEM, OBJ, TOUCH, KEYS, MAIN = [
    WORK+i*0x1000 for i in range(1, 10)]
OUT = WORK+0xf000
FUNCTION = 'func_ov007_020b1cf0'
SOURCES = ['src/func_ov007_020b1cf0.c', 'src/func_ov007_020c1da0.c',
           'src/func_ov007_020b68e8.c', 'src/func_ov007_020b6a84.c',
           'src/func_ov007_020b46b0.c', 'src/func_ov007_020b3190.c',
           'src/func_ov007_020b0a20.c', 'src/func_ov007_020b10dc.c',
           'src/func_ov007_020b16f0.c', 'src/func_ov007_020b1b74.c']
SOURCES += ['src/func_ov007_020aebac.c', 'src/func_ov007_020b0da0.c',
    'src/func_ov007_020beaa0.c', 'src/func_ov007_020be980.c',
    'src/func_ov007_020be964.c', 'src/func_ov007_020be9ac.c',
    'src/func_ov007_020bc3dc.c', 'src/func_ov007_020c9214.c',
    'src/func_ov007_020c9110.c', 'src/func_ov007_020bc02c.c',
    'src/func_ov007_020bc894.cpp', 'src/func_ov007_020b2370.cpp',
    'src/func_ov007_020c92d0.c', 'src/func_ov007_020c1d78.c']


def half(probe, address, value):
    probe.cpu.mem_write(address, struct.pack('<H', value & 0xffff))


def signed_half(probe, address):
    return struct.unpack('<h', probe.cpu.mem_read(address, 2))[0]


def prepare(probe, case):
    probe.cpu.mem_write(WORK, bytes([0xa5])*0x10000)
    for pointer, size in [(CTX, 0x188), (SUB, 16), (MAIN, 16),
                          (F4, 0x1c), (ANIM, 0x16), (ELEM, 8),
                          (OBJ, 0x14), (TOUCH, 0x44), (KEYS, 0x70)]:
        probe.cpu.mem_write(pointer, bytes(size))
    probe.words(probe.names['data_ov007_0210342c'], CTX)
    probe.words(probe.names['data_ov007_02104bec'], case['lock'])
    probe.words(CTX+4, MAIN, SUB)
    probe.words(CTX+0xf4, F4)
    probe.words(F4+0x18, ANIM)
    half(probe, ANIM+0xc, -1)  # Tail's end-of-animation gate stays inactive.
    probe.words(CTX+0x130, ELEM)
    probe.words(ELEM, OBJ)
    probe.words(OBJ+0x10, case['opacity'])
    probe.words(CTX+0x50, TOUCH, KEYS)
    half(probe, SUB+2, -1)
    probe.words(SUB+0xc, case.get('title_ticks', 0))
    half(probe, TOUCH+8, case.get('x', 128))
    half(probe, TOUCH+0xa, case.get('y', 160))
    half(probe, TOUCH+0xc, case['touch'])
    half(probe, TOUCH+0x14, case['prior_touch'])
    probe.words(TOUCH+0x24, case['idle'])
    half(probe, KEYS, case['held'])
    half(probe, KEYS+2, case.get('prior_held', 0))


def gate(probe, case):
    prepare(probe, case)
    before = bytes(probe.cpu.mem_read(WORK, 0x10000))
    observations = []

    def animation(cpu, address, size, _):
        if address == probe.names['func_ov007_020b2614']:
            observations.append([cpu.reg_read(reg) for reg in shared.REGS[:3]])
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

    hook = probe.cpu.hook_add(UC_HOOK_CODE, animation)
    probe.call(FUNCTION, [])
    probe.cpu.hook_del(hook)
    assert not probe.faults
    expected = bytearray(before)
    if case['expected'] != 'none':
        struct.pack_into('<h', expected, SUB-WORK+2, 4)
        struct.pack_into('<I', expected, CTX-WORK+0x20,
                         1 if case['expected'] == 'touch' else 0)
    assert bytes(probe.cpu.mem_read(WORK, 0x10000)) == bytes(expected), (
        probe.label, case['name'], 'unexpected game/output/canary mutation')
    pending = signed_half(probe, SUB+2)
    assert pending == (4 if case['expected'] != 'none' else -1)
    assert observations == ([[1, 0, 8]] if case['expected'] == 'touch' else [])
    assert probe.u32(probe.names['data_ov007_0210342c']) == CTX
    assert probe.u32(probe.names['data_ov007_02104bec']) == case['lock']
    return {'case': case['name'], 'pending': pending,
        'mode': probe.u32(CTX+0x20), 'animation_requests': observations,
        'game_memory_sha256': shared.base.sha(bytes(probe.cpu.mem_read(WORK, 0x10000))),
        'canaries_preserved': True, 'normal_return_preserves_stack_and_registers': True}


def observer_fixture(directory):
    source = ROOT/'tests/native_title_start_arm.cpp'
    elf = directory/'title-observer.elf'
    subprocess.run([TOOL+'g++', '-std=c++17', '-march=armv6k',
        '-mfloat-abi=hard', '-mtp=soft', '-O2', '-ffreestanding',
        '-fno-exceptions', '-fno-rtti', '-Wa,--noexecstack', '-nostdlib',
        '-Wl,-e,title_observer_entry', '-Wl,-Ttext=0x01000000', source, '-o', elf],
        check=True)
    probe = shared.Probe('guarded observer ARM11', elf=elf)
    contract = struct.unpack('<10I', probe.cpu.mem_read(
        probe.names['title_observer_contract'], 40))
    assert contract == (4, 4, 16, 2, 12, 0x44, 0x24, 0x188, 0x130, 0x10)
    # The observer receives a context pointer; declared globals live only in
    # the real executable and must never become test replacement definitions.
    assert 'data_ov007_0210342c' not in probe.names
    assert 'data_ov007_02104bec' not in probe.names
    probe.names['data_ov007_0210342c'] = WORK+0x100
    probe.names['data_ov007_02104bec'] = WORK+0x104
    case = {'lock': 0, 'opacity': 4096, 'touch': 1, 'prior_touch': 0,
            'idle': 2, 'held': 1, 'x': 128, 'y': 160}
    prepare(probe, case)
    before = bytes(probe.cpu.mem_read(WORK, 0x10000))
    assert probe.call('title_read_gate', [WORK, 0x10000, CTX, OUT]) == 1
    result = struct.unpack('<16I', probe.cpu.mem_read(OUT, 64))
    assert result == (0, 0, 0, 0, 0, 0xffffffff, 0, 4096, 128, 160, 1, 0, 2, 1, 0, 0)
    expected = bytearray(before)
    expected[OUT-WORK:OUT-WORK+64] = struct.pack('<16I', *result)
    assert bytes(probe.cpu.mem_read(WORK, 0x10000)) == bytes(expected)
    invalid = []
    pointer_cases = [('context_null', None, 0, 1),
        ('context_outside', None, WORK-0x1000, 1),
        ('context_short', None, WORK+0x10000-0x187, 1),
        ('main_null', CTX+4, 0, 2), ('sub_unmapped', CTX+8, 0x4000000, 3),
        ('element_short', CTX+0x130, WORK+0x10000-7, 4),
        ('object_unmapped', ELEM, 0x4000000, 5),
        ('touch_short', CTX+0x50, WORK+0x10000-0x27, 6),
        ('keys_short', CTX+0x54, WORK+0x10000-3, 7)]
    for name, slot, value, failure in pointer_cases:
        prepare(probe, case)
        if slot is not None:
            probe.words(slot, value)
        context = value if slot is None else CTX
        before = bytes(probe.cpu.mem_read(WORK, 0x10000))
        assert probe.call('title_read_gate', [WORK, 0x10000, context, OUT]) == 0
        out = struct.unpack('<16I', probe.cpu.mem_read(OUT, 64))
        assert out == (failure, *([0]*15))
        expected = bytearray(before)
        expected[OUT-WORK:OUT-WORK+64] = struct.pack('<16I', *out)
        assert bytes(probe.cpu.mem_read(WORK, 0x10000)) == bytes(expected)
        assert not probe.faults
        invalid.append({'case': name, 'failure': failure, 'unmapped_accesses': 0})
    spans = [(WORK, 0x10000, WORK, 0, 1),
        (WORK, 0x10000, WORK+0x10000-16, 16, 1),
        (WORK, 0x10000, WORK+0x10000-15, 16, 0),
        (WORK, 0x10000, WORK-1, 1, 0),
        (0xfffffff0, 32, 0xfffffff0, 4, 0),
        (WORK, 0x10000, 0xffffffff, 4, 0)]
    for begin, size, address, length, expected in spans:
        assert probe.call('title_span_contains', [begin, size, address, length]) == expected
    return {'contract': list(contract), 'invalid_pointer_cases': invalid,
            'span_boundary_cases': len(spans), 'out_size': 64,
            'observer_elf_sha256': shared.base.sha(elf.read_bytes()),
            'game_memory_unchanged_except_test_output': True,
            'bounds_prove_residency_not_allocation_liveness': True}


class IntroFixture:
    """Original gate, callback and animation clocks; rendering is observed.

    The recovered model driver alternates its dirty flag. The original driver,
    its original advance, BCA initialization, and callback actually execute.
    Matrix/bone/particle and camera/map services return at explicit seams;
    arithmetic seams supply signed integer operations and do not touch MMIO.
    """
    WRAPPER, MODEL, ANIMATION, TABLE = [WORK+i*0x1000 for i in (3, 4, 5, 6)]
    BCA = WORK+0x6100
    COMPONENT, MATRIX, MODEL_FILE = WORK+0x7000, WORK+0x7100, WORK+0x7200
    SECOND_MATRIX = WORK+0x7300
    TOUCH, KEYS = WORK+0x8000, WORK+0x8100
    P28, SAVE = WORK+0x9000, WORK+0x9100
    C4, C4NODE, AUX, E0, SCROLL = [WORK+i for i in (0xa000, 0xa100, 0xa200, 0xb000, 0xb100)]
    PARENT, PARENT_ANIM, OTHER = WORK+0xc000, WORK+0xc100, WORK+0xd000
    SEAMS = {'RequestScreenFade': 3, 'func_ov007_020b1fa4': 1,
        'func_ov007_020c0638': 4, 'func_ov007_020b797c': 0,
        'func_ov007_020bdeb0': 1, 'func_ov007_020bde70': 2,
        'Matrix4x3_LoadIdentity': 1, 'func_ov007_020c39f8': 4,
        'MulMat4x3Mat4x3': 3, '_ZN15ModelComponents21UpdateVertsUsingBonesEv': 1,
        '_ZN15ModelComponents11UpdateBonesEP8BCA_Filei': 3,
        'func_02044dc4': 1, 'func_ov007_020beb80': 1,
        'func_ov007_020b2614': 3, 'func_ov007_020b2764': 1}
    MATH = ['_ZN4cstd3divEii', '_ZN4cstd3modEii', '_ZN4cstd4fdivEii']

    def __init__(self, probe, bca, no_save=True):
        self.p, self.events, self.counter = probe, [], -1
        probe.cpu.mem_write(WORK, bytes([0xa5])*0x10000)
        spans = [(CTX, 0x188), (SUB, 0x10), (self.WRAPPER, 0x1c),
            (self.MODEL, 0xa0), (self.ANIMATION, 0x1c), (self.TABLE, 8),
            (self.BCA, len(bca)), (self.COMPONENT, 0x14), (self.MATRIX, 0x30),
            (self.MODEL_FILE, 0x34), (self.SECOND_MATRIX, 0x30),
            (self.TOUCH, 0x44), (self.KEYS, 0x70),
            (self.P28, 0x3c), (self.SAVE, 0x100), (self.C4, 4),
            (self.C4NODE, 8), (self.AUX, 0x10), (self.E0, 8),
            (self.SCROLL, 0x30), (self.PARENT, 0xa0), (self.PARENT_ANIM, 0x1c),
            (self.OTHER, 0x30)]
        for pointer, size in spans:
            probe.cpu.mem_write(pointer, bytes(size))
        probe.words(probe.names['data_ov007_0210342c'], CTX)
        probe.words(probe.names['data_ov007_02104bec'], 1)
        probe.words(CTX+8, SUB)
        probe.words(CTX+0x28, self.P28)
        probe.words(self.P28, self.SAVE)
        if not no_save:
            probe.cpu.mem_write(self.SAVE+0xb, b'\x01')
        probe.words(CTX+0x50, self.TOUCH, self.KEYS)
        probe.words(CTX+0x100, self.WRAPPER)
        probe.words(CTX+0xc4, self.C4)
        probe.words(self.C4, self.C4NODE)
        probe.words(self.C4NODE+4, self.AUX)
        probe.words(CTX+0xe0, self.E0)
        probe.words(CTX+0xcc, self.E0)
        probe.words(self.E0+4, self.SCROLL)
        probe.words(self.WRAPPER+4, self.MODEL)
        probe.words(self.MODEL, self.COMPONENT)
        probe.words(self.MODEL+4, self.MATRIX, self.SECOND_MATRIX)
        probe.words(self.MODEL+0x14, self.TABLE, self.ANIMATION)
        probe.words(self.MODEL+0x9c, 2)
        probe.words(self.COMPONENT, self.MODEL_FILE)
        probe.words(self.COMPONENT+0xc, self.MATRIX)
        probe.words(self.TABLE+4, self.BCA)
        probe.cpu.mem_write(self.BCA, bca)
        probe.words(self.PARENT+0x18, self.PARENT_ANIM)
        half(probe, self.ANIMATION+0xc, -1)
        half(probe, SUB, 11)
        half(probe, SUB+2, -1)
        half(probe, SUB+8, -1)
        for name in ['data_ov007_02103340', 'data_ov007_02103344']:
            probe.words(probe.names[name], 0)
        self.before = bytes(probe.cpu.mem_read(WORK, 0x10000))
        self.root_before = probe.u32(probe.names['data_ov007_0210342c'])
        self.hook = probe.cpu.hook_add(UC_HOOK_CODE, self.observe)

    @staticmethod
    def signed(value):
        return value - 0x100000000 if value & 0x80000000 else value

    def observe(self, cpu, address, size, _):
        p = self.p
        for name, count in self.SEAMS.items():
            if address == p.names[name]:
                args = [cpu.reg_read(reg) for reg in shared.REGS[:count]]
                if name == 'RequestScreenFade' and args[2]:
                    args[2] = next((n for n, a in p.names.items() if a == args[2]), hex(args[2]))
                if name in ['RequestScreenFade', 'func_ov007_020bdeb0', 'func_ov007_020bde70']:
                    self.events.append({'counter': self.counter, 'service': name, 'arguments': args})
                cpu.reg_write(shared.REGS[0], 0)
                cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
                return
        for name in self.MATH:
            if address == p.names[name]:
                a, b = [self.signed(cpu.reg_read(reg)) for reg in shared.REGS[:2]]
                assert b != 0
                numerator = a << 12 if name.endswith('4fdivEii') else a
                q = abs(numerator)//abs(b)
                if (numerator < 0) != (b < 0): q = -q
                result = a-q*b if name.endswith('3modEii') else q
                cpu.reg_write(shared.REGS[0], result & 0xffffffff)
                cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
                return

    def inputs(self, touch=0, x=128, y=96, held=0, prior=0):
        half(self.p, self.TOUCH+8, x)
        half(self.p, self.TOUCH+0xa, y)
        half(self.p, self.TOUCH+0xc, touch)
        half(self.p, self.TOUCH+0x14, 0)
        half(self.p, self.KEYS, held)
        half(self.p, self.KEYS+2, prior)

    def check_canaries(self):
        expected = bytearray(self.before)
        # These objects/fields are the documented original writes; all gaps,
        # other objects, BCA bytes, source tables and allocation tails survive.
        mutable = [(SUB, 0x10), (self.WRAPPER+0x18, 4), (self.MODEL+0x40, 0x4c),
            (self.MODEL+0x94, 8), (self.ANIMATION, 0x1c), (self.COMPONENT+0xc, 4),
            (self.MATRIX+0x24, 0xc),
            (self.TOUCH+8, 0xe), (self.KEYS, 4), (self.AUX+2, 2),
            (self.SCROLL+0x20, 0x10), (CTX+0xf8, 4)]
        for address, size in mutable:
            expected[address-WORK:address-WORK+size] = self.p.cpu.mem_read(address, size)
        actual = bytes(self.p.cpu.mem_read(WORK, 0x10000))
        assert actual == bytes(expected), [(hex(WORK+i), a, b)
            for i, (a, b) in enumerate(zip(actual, expected)) if a != b][:32]
        assert self.root_before == self.p.u32(self.p.names['data_ov007_0210342c'])
        assert not self.p.faults

    def snapshot(self):
        p = self.p
        return {'counter': self.counter, 'pending': signed_half(p, SUB+2),
            'callback_active': p.u32(self.WRAPPER+0x18) != 0,
            'flags': p.u32(SUB+4), 'pause': p.u32(self.ANIMATION),
            'frame': struct.unpack('<H', p.cpu.mem_read(self.ANIMATION+0x10, 2))[0],
            'total': struct.unpack('<H', p.cpu.mem_read(self.ANIMATION+0x12, 2))[0],
            'fixed_frame': p.u32(self.ANIMATION+0x14),
            'speed': p.u32(self.ANIMATION+0x18), 'dirty': p.u32(self.MODEL+0x98)}

    def run(self, name, tap=None, key=None, steps=600):
        detach, completion, checkpoints = None, None, []
        for tick in range(steps):
            self.counter = tick
            self.p.call('func_ov007_020c92d0', [SUB])
            assert self.p.u32(SUB+0xc) == tick
            self.inputs(touch=int(tick == tap), held=(key[1] if key and tick == key[0] else 0))
            self.p.call('func_ov007_020b0a20', [])
            if signed_half(self.p, SUB+2) == 10:
                completion = self.snapshot()
                break
            self.p.call('func_ov007_020be9ac', [self.WRAPPER, self.PARENT, self.OTHER])
            if detach is None and tick > 0 and not self.p.u32(self.WRAPPER+0x18):
                detach = tick
            if tick in [4, 20, 26, 40, 60, 100, 206, 300, 360, steps-1]:
                checkpoints.append(self.snapshot())
        self.check_canaries()
        self.p.cpu.hook_del(self.hook)
        return {'case': name, 'tap': tap, 'key': key, 'callback_detach': detach,
            'completion': completion, 'checkpoints': checkpoints, 'events': self.events,
            'canaries_preserved': True, 'normal_returns_preserve_stack_registers': True}


def intro_cases(rom, native, cart):
    address, _, image = cart.image(7)
    table = rom.names['data_ov007_020d7584']-address
    _, handles = cart.handles()
    bcas, assets = {}, []
    for index, expected, frames in [(14, 'data/DSMT/opening_star.bca', 157),
                                     (15, 'data/DSMT/opening_star_short.bca', 90)]:
        handle = struct.unpack_from('<H', image, table+index*2)[0]
        path = cart.entries[handles[handle]][4]
        assert path == expected
        packed = cart.file(path)
        raw = ndspy.lz10.decompress(packed[4:]) if packed[:4] == b'LZ77' else packed
        assert struct.unpack_from('<H', raw, 2)[0] == frames
        bcas[index] = raw
        assets.append({'table_index': index, 'handle': handle, 'path': path,
            'frames': frames, 'flags': raw[4], 'packed_sha256': shared.base.sha(packed),
            'decompressed_sha256': shared.base.sha(raw)})
    cases = []
    for name, no_save, tap, key, steps, expected in [
        ('new_cart_no_touch_keeps_wait', True, None, None, 360, None),
        ('new_cart_A_does_not_skip', True, None, (206, 1), 360, None),
        ('new_cart_B_does_not_skip', True, None, (206, 2), 360, None),
        ('new_cart_touch26_original_half_render_rate', True, 26, None, 400, 344),
        ('new_cart_touch4_original_half_render_rate', True, 4, None, 400, 324),
        ('existing_file_short_natural', False, None, None, 220, 171),
        ('existing_file_A_skip', False, None, (4, 1), 10, 4),
        ('existing_file_B_skip', False, None, (4, 2), 10, 4),
        ('existing_file_key_before4_no_skip', False, None, (3, 2), 220, 171)]:
        raw = bcas[14 if no_save else 15]
        left = IntroFixture(rom, raw, no_save).run(name, tap, key, steps)
        right = IntroFixture(native, raw, no_save).run(name, tap, key, steps)
        assert left == right, (name, left, right)
        assert (left['completion']['counter'] if left['completion'] else None) == expected, (name, left)
        if tap == 26:
            assert left['callback_detach'] == 40
            mid = next(s for s in left['checkpoints'] if s['counter'] == 206)
            assert mid['frame'] == 82 and mid['speed'] == 4096 and mid['pending'] == -1
            assert [e['counter'] for e in left['events'] if e['service'] == 'RequestScreenFade'
                    and e['arguments'][0] == 3] == [294, 295]
        cases.append(left)
    bounds = []
    for tick, x, y, down, accepts in [(3,128,96,1,False), (4,128,96,0,False),
        (4,95,96,1,False), (4,161,96,1,False), (4,128,63,1,False),
        (4,128,129,1,False), (4,96,64,1,True), (4,160,128,1,True),
        (4,128,96,1,True)]:
        values = []
        for probe in [rom, native]:
            f = IntroFixture(probe, bcas[14])
            f.counter = tick
            probe.words(SUB+0xc, tick)
            probe.words(SUB+4, 1)
            probe.words(f.WRAPPER+0x18, probe.names['func_ov007_020b0da0'])
            f.inputs(touch=down, x=x, y=y)
            # A held touch is deliberately also accepted: original never reads priorDown here.
            half(probe, f.TOUCH+0x14, 1)
            probe.call('func_ov007_020b0da0', [f.WRAPPER])
            assert bool(probe.u32(SUB+4) & 2) == accepts
            f.check_canaries()
            values.append({'flags': probe.u32(SUB+4),
                'tap_counter': probe.u32(probe.names['data_ov007_02103340']),
                'events': f.events})
            probe.cpu.hook_del(f.hook)
        assert values[0] == values[1]
        bounds.append({'counter': tick, 'x': x, 'y': y, 'down': down,
            'prior_down': 1, 'accepted': accepts, 'canaries_preserved': True})
    no_save_checks = []
    for bytes_at_b, flag in [([0,0,0],0), ([1,0,0],0), ([0,1,0],0),
                            ([0,0,1],0), ([0,0,0],1), ([0,0,0],0x80000000)]:
        results = []
        for probe in [rom, native]:
            f = IntroFixture(probe, bcas[14])
            probe.cpu.mem_write(f.SAVE+0xb, bytes(bytes_at_b))
            probe.words(f.SAVE+4, flag)
            before = bytes(probe.cpu.mem_read(WORK, 0x10000))
            results.append(probe.call('func_ov007_020aebac', []))
            assert before == bytes(probe.cpu.mem_read(WORK, 0x10000))
            probe.cpu.hook_del(f.hook)
        assert results[0] == results[1] == int(bytes_at_b == [0,0,0] and flag == 0)
        no_save_checks.append({'slots': bytes_at_b, 'word4': flag, 'result': results[0]})
    phases = []
    for probe in [rom, native]:
        f = IntroFixture(probe, bcas[14])
        for count in [59, 60, 61]:
            f.counter = count
            probe.words(SUB+0xc, count)
            probe.call('func_ov007_020b10dc', [])
        assert f.events == [{'counter': 60, 'service': 'RequestScreenFade',
                             'arguments': [5, 3, 'func_ov007_020b16f0']}]
        probe.call('func_ov007_020b16f0', [])
        assert signed_half(probe, SUB+2) == 1
        assert probe.u32(probe.names['data_ov007_02104bec']) == 0
        half(probe, SUB+2, -1)
        probe.words(SUB+0xc, 30)
        probe.call('func_ov007_020b1b74', [])
        assert signed_half(probe, SUB+2) == -1
        probe.words(SUB+0xc, 31)
        probe.call('func_ov007_020b1b74', [])
        assert signed_half(probe, SUB+2) == 0
        f.check_canaries()
        probe.cpu.hook_del(f.hook)
        phases.append({'fade_request_counter': 60, 'fade_type': 5, 'duration': 3,
            'callback': 'func_ov007_020b16f0', 'callback_sets_pending': 1,
            'callback_unlocks_input': True, 'settle30_pending': -1, 'settle31_pending': 0})
    assert phases[0] == phases[1]
    return {'assets': assets, 'cases': cases, 'star_touch_bounds': bounds,
        'no_save_predicate': no_save_checks, 'post_BCA_phases': phases[0],
        'seams': list(IntroFixture.SEAMS), 'arithmetic_seams': IntroFixture.MATH,
        'clock': 'Actual be980/bc3dc/c9214 initialization, original be9ac/bc02c/bc894/c9110 '
                 'and b0da0 callback, original c92d0 counter; graphics services are observed, '
                 'not rendered; complete frames and generic fade clocks are outside this test.',
        'animation_offsets': {'pause': 0, 'flags': 4, 'dirty': 8, 'index': 12,
            'current_frame': 16, 'frame_count': 18, 'fixed_frame': 20, 'speed': 24},
        'checkpoint_minimum': 'ctx188; ctx28 wrapper3c→save first0xe; statesctx4/8/c; '
            'ctx100 wrapper1c→+4 modela0→+18 animation1c; model14→BCA pointer table; '
            'model0→components14→+c bone matrices; model98/9c; wrapper18 callback; '
            'ctx50 touch44/ctx54 keys70; globals02103340/344; inputlock02104bec.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, default=ROOT/
        'build/native-runtime-azahar/attempt-1791611477172386000/sm64ds-native.elf')
    parser.add_argument('--pack', type=Path, default=ROOT/'build/europe.pak')
    parser.add_argument('--report', type=Path, default=ROOT/'build/native-runtime/title-start-test.json')
    args = parser.parse_args()
    assert subprocess.check_output(['git', '-C', ROOT/'upstream', 'rev-parse', 'HEAD'],
                                   text=True).strip() == PIN
    cart = shared.base.Cartridge(args.pack)
    rom, native = shared.Probe('ROM', cart=cart), shared.Probe('ARM11', elf=args.elf)
    elf_hash = shared.base.sha(args.elf.read_bytes())
    base_case = {'lock': 0, 'opacity': 4096, 'touch': 0,
                 'prior_touch': 0, 'idle': 2, 'held': 0, 'expected': 'none'}
    cases = []
    for name, changes in [('A', {'held': 1}), ('B_fresh', {'held': 2, 'expected': 'B'}),
        ('B_held', {'held': 2, 'prior_held': 2}),
        ('touch_fresh', {'touch': 1, 'expected': 'touch'}),
        ('touch_held', {'touch': 1, 'prior_touch': 1}),
        ('touch_no_idle', {'touch': 1, 'idle': 0}),
        ('touch_not_opaque', {'touch': 1, 'opacity': 4095}),
        ('touch_locked', {'touch': 1, 'lock': 1}),
        ('B_locked', {'held': 2, 'lock': 1}),
        ('touch_left_top', {'touch': 1, 'x': 0, 'y': 0, 'expected': 'touch'}),
        ('touch_right_bottom', {'touch': 1, 'x': 255, 'y': 191, 'expected': 'touch'}),
        ('touch_idle_one', {'touch': 1, 'idle': 1, 'expected': 'touch'})]:
        case = {**base_case, **changes, 'name': name}
        left, right = gate(rom, case), gate(native, case)
        assert left == right, (left, right)
        cases.append({'input': case, 'ROM_and_ARM11': left})
    with tempfile.TemporaryDirectory(prefix='sm64ds-title-observer-') as temp:
        safe = observer_fixture(Path(temp))
    intro = intro_cases(rom, native, cart)
    hashes = {str(path.relative_to(ROOT)): shared.base.sha(path.read_bytes())
        for path in [*[ROOT/'upstream'/name for name in SOURCES],
            ROOT/'upstream/config/arm9/overlays/ov007/symbols.txt',
            ROOT/'upstream/include/Heap.h', ROOT/'source/ctr/game_heap.cpp',
            ROOT/'source/game_port/native_frame.cpp', ROOT/'source/core/game_input.cpp',
            ROOT/'source/ctr/input.cpp', ROOT/'tests/native_title_start_arm.cpp',
            ROOT/'tools/test_native_title_start_arm.py', ROOT/'tools/test_native_audio_deferred_arm.py']}
    assert elf_hash == shared.base.sha(args.elf.read_bytes())
    report = {'status': 'passed', 'upstream_commit': PIN,
        'immutable_elf': str(args.elf), 'elf_sha256': elf_hash,
        'source_sha256': hashes, 'cases': cases, 'guarded_observer': safe,
        'intro_phases': intro,
        'ABI': {'pointer_bytes': 4, 'lock_element_bytes': 4,
            'linkage': 'unmangled C BSS aliases; A in native nm output',
            'context_slot': {'ROM': hex(rom.names['data_ov007_0210342c']),
                             'ARM11': hex(native.names['data_ov007_0210342c'])},
            'lock_array': {'ROM': hex(rom.names['data_ov007_02104bec']),
                           'ARM11': hex(native.names['data_ov007_02104bec'])},
            'context_allocation_bytes': 0x188,
            'opacity_path': 'context+0x130 -> element[0] -> object+0x10'},
        'animation_seam': 'Observe func_ov007_020b2614 arguments then return; no animation execution',
        'scope': 'Isolated original title-start gate and test-only guarded reads in host ARM CPU model; '
                 'no game frame, menu usability, Azahar, playback, physical console or FPS proof.'}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({'status': 'passed', 'ROM_ARM11_gate_cases': len(cases),
        'safe_pointer_negative_cases': len(safe['invalid_pointer_cases']),
        'intro_clock_cases': len(intro['cases']),
        'intro_touch_boundary_cases': len(intro['star_touch_bounds']),
        'span_boundary_cases': safe['span_boundary_cases'], 'report': str(args.report)}))


if __name__ == '__main__':
    main()
