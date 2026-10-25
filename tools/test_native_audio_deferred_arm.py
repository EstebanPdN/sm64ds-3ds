#!/usr/bin/env python3
"""Regress the observed null-SDAT fault and the declared disabled audio policy.

Executes immutable baseline ARM11, original ROM, and a small ARM11 executable
containing the production policy and unchanged menu/player callers. The ROM's
empty SEQARC table demonstrates the actual failure return; no playback, frame,
Azahar, physical console, or FPS evidence is produced. Cartridge bytes remain
in memory and are not included in reports or exported fixture files.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'build/game-data/python'))
import test_native_menu_callbacks as base
from test_native_menu_constructors import original_symbols
from unicorn import (Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE,
                     UC_HOOK_MEM_INVALID)
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,
    UC_ARM_REG_CPSR, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC)

ROOT, TOOL = base.ROOT, base.TOOL
PIN = '6230b2996636e3fd0e03cb178161c94cb4b73cc9'
STACK, STOP = 0x18000000, 0x18010000
WORK, HANDLES, VALUES, ROOT_RECORD, INFO = (0x12000000, 0x12000100,
    0x12000800, 0x12001000, 0x12002000)
CALLERS = ['src/func_ov007_020be098.c', 'src/Player_PlaySoundEffect.c',
           'src/func_0204f94c.c']
LOOKUPS = ['src/func_02050c14.cpp', 'src/func_02051f1c.c',
           'src/func_02051e60.c', 'src/func_020518dc.c']
REGS = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)


class Probe:
    def __init__(self, label, elf=None, cart=None):
        self.label = label
        self.cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        self.cpu.reg_write(UC_ARM_REG_C1_C0_2, 0xf << 20)
        self.cpu.reg_write(UC_ARM_REG_FPEXC, 1 << 30)
        if elf:
            self.names = base.symbols(elf)
            raw = elf.read_bytes()
            assert raw[:7] == b'\x7fELF\x01\x01\x01'
            offset = struct.unpack_from('<I', raw, 28)[0]
            size, count = struct.unpack_from('<HH', raw, 42)
            loads = [struct.unpack_from('<8I', raw, offset+i*size)
                     for i in range(count)]
            loads = [row for row in loads if row[0] == 1]
            begin = min(row[2] for row in loads) & ~0xfff
            end = (max(row[2]+row[5] for row in loads)+0xfff) & ~0xfff
            self.cpu.mem_map(begin, end-begin)
            for _, at, address, _, filesz, _, _, _ in loads:
                self.cpu.mem_write(address, raw[at:at+filesz])
        else:
            self.names = original_symbols()
            self.cpu.mem_map(0x02000000, 0x200000)
            for module in (0xffffffff, 7):
                address, _, image = cart.image(module)
                self.cpu.mem_write(address, image)
        self.cpu.mem_map(WORK, 0x10000)
        self.cpu.mem_map(STACK, 0x20000)
        self.faults, self.entries = [], []
        self.cpu.hook_add(UC_HOOK_MEM_INVALID, self.invalid)
        self.cpu.hook_add(UC_HOOK_CODE, self.code)

    def u32(self, address):
        return struct.unpack('<I', self.cpu.mem_read(address, 4))[0]

    def words(self, address, *values):
        self.cpu.mem_write(address, struct.pack('<'+'I'*len(values),
            *(value & 0xffffffff for value in values)))

    def invalid(self, cpu, access, address, size, value, _):
        self.faults.append({'pc': hex(cpu.reg_read(UC_ARM_REG_PC)),
            'address': hex(address), 'bytes': size, 'access': access})
        return False

    def code(self, cpu, address, size, _):
        for name, count in [('func_02051f1c', 3), ('func_02051e60', 6),
                            ('func_020518dc', 2)]:
            if address == self.names.get(name):
                args = [cpu.reg_read(reg) for reg in REGS[:min(count, 4)]]
                if count > 4:
                    sp = cpu.reg_read(UC_ARM_REG_SP)
                    args += [self.u32(sp+i*4) for i in range(count-4)]
                self.entries.append({'function': name, 'arguments': args})

    def reset(self, index, table, empty_metadata=False, enabled=True, bank=7):
        self.faults, self.entries = [], []
        self.cpu.mem_write(WORK, bytes([0xa5])*0x10000)
        self.words(self.names['data_ov007_02104bbc'], HANDLES)
        self.cpu.mem_write(self.names['data_ov007_02103134'], table)
        self.cpu.mem_write(self.names['data_0209b480'], bytes([int(enabled)]))
        self.cpu.mem_write(self.names['data_0208e428'], bytes([bank & 255]))
        self.words(self.names['data_020a5bb8'], ROOT_RECORD if empty_metadata else 0)
        if empty_metadata:
            self.cpu.mem_write(ROOT_RECORD, bytes(0x100))
            self.cpu.mem_write(INFO, bytes(0x100))
            self.words(ROOT_RECORD+0x84, INFO)
        if 'audio_deferred_count' in self.names:
            self.words(self.names['audio_deferred_count'], 0)
            self.words(self.names['audio_deferred_operation'], 0)

    def call(self, name, arguments, expect_fault=False, instruction_limit=100000):
        self.cpu.reg_write(UC_ARM_REG_CPSR, 0x10)
        self.cpu.mem_write(STACK+0xe000, bytes([0x6d])*0x2000)
        sp = STACK+0xf000
        for i, value in enumerate((*arguments, 0, 0, 0, 0)[:4]):
            self.cpu.reg_write(REGS[i], value & 0xffffffff)
        for i, value in enumerate(arguments[4:]):
            self.words(sp+i*4, value)
        incoming_stack = bytes(self.cpu.mem_read(sp, 64))
        self.cpu.reg_write(UC_ARM_REG_SP, sp)
        self.cpu.reg_write(UC_ARM_REG_LR, STOP)
        for i, reg in enumerate(base.SAVED):
            self.cpu.reg_write(reg, 0x12340000+i)
        try:
            self.cpu.emu_start(self.names[name], STOP, count=instruction_limit)
        except UcError:
            assert expect_fault and self.faults, (self.label, name, self.faults)
        if expect_fault:
            assert len(self.faults) == 1
            assert self.faults[0]['address'] == '0x84' and self.faults[0]['bytes'] == 4
            assert int(self.faults[0]['pc'], 16) == self.names['func_02050c14']+8
            return None
        assert not self.faults, (self.label, name, self.faults)
        assert self.cpu.reg_read(UC_ARM_REG_PC) == STOP, (self.label, name, 'instruction bound')
        assert self.cpu.reg_read(UC_ARM_REG_SP) == sp
        assert all(self.cpu.reg_read(reg) == 0x12340000+i
                   for i, reg in enumerate(base.SAVED))
        assert bytes(self.cpu.mem_read(sp, 64)) == incoming_stack
        assert bytes(self.cpu.mem_read(sp-0x1000, 32)) == bytes([0x6d])*32
        return self.cpu.reg_read(UC_ARM_REG_R0)


def build_policy(directory):
    flags = ['-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard',
             '-mtp=soft', '-O2', '-ffreestanding',
             '-Wa,--noexecstack', '-I'+str(ROOT/'include')]
    sources = [(ROOT/'upstream'/name, 'c') for name in CALLERS]
    sources += [(ROOT/'source/game_port/native_audio_deferred.cpp', 'c++'),
                (ROOT/'tests/native_audio_deferred_arm.cpp', 'c++')]
    objects = []
    for index, (source, language) in enumerate(sources):
        obj = directory/f'{index}.o'
        subprocess.run([TOOL+('gcc' if language == 'c' else 'g++'),
                        *flags, *(['-fno-exceptions', '-fno-rtti']
                            if language == 'c++' else []),
                        '-c', source, '-o', obj], check=True)
        objects.append(obj)
    elf = directory/'audio-policy.elf'
    subprocess.run([TOOL+'g++', *flags, '-nostdlib',
        '-Wl,-e,audio_test_entry', '-Wl,-Ttext=0x01000000',
        *objects, '-o', elf], check=True)
    return elf


def check_state(probe, before, reset_index=None):
    after = bytes(probe.cpu.mem_read(WORK, 0x10000))
    expected = bytearray(before)
    if reset_index is not None:
        struct.pack_into('<I', expected, HANDLES-WORK+reset_index*4, 0)
    assert after == bytes(expected), (probe.label, 'handle/canary mutation')
    assert probe.u32(probe.names['data_020a5bb8']) == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-elf', type=Path, default=ROOT/
        'build/native-runtime-azahar/attempt-1791610976045409000/sm64ds-native.elf')
    parser.add_argument('--report', type=Path, default=ROOT/
        'build/native-runtime/audio-deferred-test.json')
    parser.add_argument('--pack', type=Path, default=ROOT/'build/europe.pak')
    args = parser.parse_args()
    assert subprocess.check_output(['git', '-C', ROOT/'upstream', 'rev-parse', 'HEAD'],
                                   text=True).strip() == PIN
    cart = base.Cartridge(args.pack)
    rom = Probe('ROM', cart=cart)
    baseline = Probe('baseline ARM11', elf=args.baseline_elf)
    # Use the actual immutable OV007 sequence-ID table, in memory only.
    start, _, image = cart.image(7)
    at = rom.names['data_ov007_02103134']-start
    table = image[at:at+128*4]
    assert len(table) == 128*4
    baseline_hash = base.sha(args.baseline_elf.read_bytes())
    negatives, callers, leaves, empty_returns = [], [], [], []
    with tempfile.TemporaryDirectory(prefix='sm64ds-audio-policy-') as temp:
        policy_elf = build_policy(Path(temp))
        policy = Probe('production policy ARM11', elf=policy_elf)
        policy_hash = base.sha(policy_elf.read_bytes())
        # All three direct SEQARC routes reproduce the same owning null root.
        tests = [('func_ov007_020be098', [0]),
                 ('Player_PlaySoundEffect', [HANDLES+16, 2, 19]),
                 ('Player_PlaySoundEffect', [HANDLES+16, 3, 19]),
                 ('func_020518dc', [2, VALUES])]
        for name, arguments in tests:
            for probe in (rom, baseline):
                probe.reset(0, table)
                probe.call(name, arguments, expect_fault=True)
                negatives.append({'implementation': probe.label, 'caller': name,
                    'arguments': arguments, 'fault': probe.faults[0],
                    'provider_entries': probe.entries})
            policy.reset(0, table)
            before = bytes(policy.cpu.mem_read(WORK, 0x10000))
            result = policy.call(name, arguments)
            check_state(policy, before, 0 if name == 'func_ov007_020be098' else None)
            assert result == 0
            assert policy.u32(policy.names['audio_deferred_count']) == 1
            callers.append({'caller': name, 'return': result,
                'provider_entries': policy.entries, 'deferred_calls': 1,
                'root_stays_null': True, 'handle_mutation': 'original reset only'
                    if name == 'func_ov007_020be098' else 'none'})
        for index in (1, 53, 56, 127):
            policy.reset(index, table)
            before = bytes(policy.cpu.mem_read(WORK, 0x10000))
            assert policy.call('func_ov007_020be098', [index]) == 0
            check_state(policy, before, index)
            assert policy.entries == [{'function': 'func_02051f1c',
                'arguments': [HANDLES+index*4, 2,
                    struct.unpack_from('<I', table, index*4)[0]]}]
            assert policy.u32(policy.names['audio_deferred_count']) == 1
            callers.append({'caller': 'func_ov007_020be098', 'index': index,
                'return': 0, 'provider_entries': policy.entries,
                'handle_mutation': 'original reset only'})
        # Original failure exits with a real root and an absent SEQARC record.
        # This is an explicit metadata fixture, not a substitute in production.
        for name, arguments in [('func_02051f1c', [HANDLES, 2, 19]),
            ('func_02051e60', [HANDLES, -1, 7, -1, 3, 19]),
            ('func_020518dc', [2, VALUES])]:
            for probe in (rom, baseline):
                probe.reset(0, table, empty_metadata=True)
                before = bytes(probe.cpu.mem_read(WORK, 0x10000))
                assert probe.call(name, arguments) == 0
                assert bytes(probe.cpu.mem_read(WORK, 0x10000)) == before
                empty_returns.append({'implementation': probe.label,
                    'function': name, 'return': 0, 'output_unchanged': True})
        for bank in (0, 7, 127, 128, 255):
            for kind in (0, 1, 2, 3, 0xffffffff):
                policy.reset(0, table, bank=bank)
                before = bytes(policy.cpu.mem_read(WORK, 0x10000))
                result = policy.call('Player_PlaySoundEffect', [HANDLES+16, kind, 0x7fffffff])
                assert result == 0
                target = 'func_02051e60' if kind == 3 else 'func_02051f1c'
                expected = [HANDLES+16, 0xffffffff,
                    (bank if bank < 128 else bank-256) & 0xffffffff,
                    0xffffffff, 3, 0x7fffffff] if kind == 3 else [HANDLES+16, kind, 0x7fffffff]
                assert policy.entries == [{'function': target, 'arguments': expected}]
                check_state(policy, before)
                callers.append({'caller': 'Player_PlaySoundEffect', 'kind': kind,
                    'bank_byte': bank, 'return': result, 'provider_entries': policy.entries})
        for values in ([0, 1, 2, 3, 4], [0xffffffff]*5,
                       [0x80000000, 0x7fffffff, 0x12345678, 0xffffffff, 0x80000000]):
            policy.reset(0, table)
            policy.words(VALUES, *values)
            inputs = bytes(policy.cpu.mem_read(VALUES-32, 96))
            before = bytes(policy.cpu.mem_read(WORK, 0x10000))
            assert policy.call('audio_six_argument_caller', [HANDLES, VALUES]) == 0
            assert policy.entries == [{'function': 'func_02051e60',
                                       'arguments': [HANDLES, *values]}]
            assert bytes(policy.cpu.mem_read(VALUES-32, 96)) == inputs
            check_state(policy, before)
            leaves.append({'function': 'audio_six_argument_caller',
                           'arguments': [HANDLES, *values], 'return': 0,
                           'stack_arguments_checked': [5, 6]})
        # Disabled game gate keeps its original skip result and never calls
        # the backend. That return is caller policy, not playback success.
        policy.reset(0, table, enabled=False)
        assert policy.call('Player_PlaySoundEffect', [HANDLES, 3, 19]) == 1
        assert policy.entries == [] and policy.u32(policy.names['audio_deferred_count']) == 0
        assert policy_hash == base.sha(policy_elf.read_bytes())
    assert baseline_hash == base.sha(args.baseline_elf.read_bytes())
    source_hashes = {str(path.relative_to(ROOT)): base.sha(path.read_bytes()) for path in
        [ROOT/'upstream'/name for name in CALLERS+LOOKUPS]+[
            ROOT/'source/game_port/native_audio_deferred.cpp',
            ROOT/'tests/native_audio_deferred_arm.cpp', Path(__file__) ]}
    report = {'status': 'passed', 'upstream_commit': PIN,
        'baseline_elf_sha256': baseline_hash, 'policy_fixture_elf_sha256': policy_hash,
        'source_sha256': source_hashes, 'baseline_negative_controls': negatives,
        'original_absent_record_returns': empty_returns, 'real_caller_cases': callers,
        'six_argument_caller_cases': leaves,
        'normal_returns_preserve_stack_and_callee_saved_registers': True,
        'no_sdat_root_or_metadata_manufactured': True, 'handle_canaries_checked': True,
        'audio_disabled_original_skip': {'return': 1, 'backend_calls': 0},
        'scope': 'Host original ROM and ARM11 instructions; disabled audio boundary '
                 'and unchanged callers only. No game frame, Azahar, physical console, playback or FPS proof.'}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({'status': report['status'], 'negative_controls': len(negatives),
        'original_failure_exits': len(empty_returns), 'caller_cases': len(callers),
        'six_argument_cases': len(leaves), 'report': str(args.report)}))


if __name__ == '__main__':
    main()
