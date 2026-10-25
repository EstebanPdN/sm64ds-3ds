#!/usr/bin/env python3
"""Compare original ROM and production ARM11 billboard command submission.

The original billboard and all its matrix/math callees execute. Only the DS
integer arithmetic device and the zero-callback, immediately-awaited SDK FIFO
transport are modeled. The native side executes its real command buffer and
DMA wait with DS MMIO unmapped. Fixtures and display lists are read-only and
checked in full; reports contain hashes, never cartridge payloads. This gate
does not establish a rendered frame, IRQ timing, Azahar or console performance.
"""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess

import ndspy.code
import test_native_audio_deferred_arm as shared
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP

base, ROOT, WORK = shared.base, shared.ROOT, shared.WORK
SELF, OBJECT, ENTRIES, PARTS, IDS, BONE_IDS, MATRICES, MATRIX, LIGHT, LIST = (
    WORK+0x100, WORK+0x200, WORK+0x300, WORK+0x400, WORK+0x500,
    WORK+0x600, WORK+0x800, WORK+0x1000, WORK+0x1100, WORK+0x1200)
MASK64 = (1 << 64)-1
IDENTITY = (4096, 0, 0, 0, 4096, 0, 0, 0, 4096, 0, 0, 0)
FUNCTION = 'func_02044534'


def original_machine(cart):
    probe = shared.Probe('ASMP0 original ARM9', cart=cart)
    address, _, image = cart.image(0xffffffff)
    for section in ndspy.code.MainCodeFile(image, address).sections:
        if section.ramAddress < 0x02000000 and section.data:
            begin = section.ramAddress & ~0xfff
            end = (section.ramAddress+len(section.data)+0xfff) & ~0xfff
            probe.cpu.mem_map(begin, end-begin)
            probe.cpu.mem_write(section.ramAddress, bytes(section.data))
    probe.cpu.mem_map(0x04000000, 4096)

    def arithmetic(cpu, access, address, size, value, _):
        if address in (0x040002a0, 0x040002a4):
            numerator = struct.unpack('<q', cpu.mem_read(0x04000290, 8))[0]
            mode = struct.unpack('<H', cpu.mem_read(0x04000280, 2))[0] & 3
            denominator = struct.unpack('<i' if mode == 1 else '<q',
                cpu.mem_read(0x04000298, 4 if mode == 1 else 8))[0]
            quotient = (-1 if numerator >= 0 else 1) if denominator == 0 else (
                abs(numerator)//abs(denominator) * (-1 if (numerator < 0) != (denominator < 0) else 1))
            cpu.mem_write(0x040002a0, struct.pack('<Q', quotient & MASK64))
        elif address == 0x040002b4:
            radicand = struct.unpack('<Q', cpu.mem_read(0x040002b8, 8))[0]
            mode = struct.unpack('<H', cpu.mem_read(0x040002b0, 2))[0]
            if not mode & 1: radicand &= 0xffffffff
            cpu.mem_write(address, struct.pack('<I', math.isqrt(radicand)))
    probe.cpu.hook_add(UC_HOOK_MEM_READ, arithmetic,
                       begin=0x040002a0, end=0x040002b7)
    return probe


def fixture(case):
    raw = bytearray([0xa5])*0x10000
    def put(address, *values):
        struct.pack_into('<'+'I'*len(values), raw, address-WORK,
                         *(v & 0xffffffff for v in values))
    put(SELF, OBJECT, 0, 0, MATRICES)
    put(OBJECT, case.get('shift', 1))
    put(OBJECT+0x10, ENTRIES); put(OBJECT+0x2c, BONE_IDS)
    # Different neighboring entry and part detect an accidental table index
    # or part stride change instead of mirroring the tested implementation.
    put(ENTRIES, 1, PARTS+16, 1, PARTS+16)
    put(ENTRIES+case.get('index', 0)*8, case.get('entries', 1), PARTS)
    put(PARTS, case.get('bones', 1), IDS, 40, LIST)
    put(PARTS+16, 1, IDS+8, 8, LIST+0x80)
    raw[IDS-WORK:IDS-WORK+4] = bytes([2, 0, 1, 3])
    raw[IDS-WORK+8] = 3
    struct.pack_into('<4H', raw, BONE_IDS-WORK, 1, 2, 0, 3)
    matrices = [list(IDENTITY) for _ in range(4)]
    matrices[0][3:6] = case.get('up', (0, 4096, 0))
    matrices[0][9:12] = case.get('translation', (12288, -8192, 4096))
    matrices[1][3:6] = (-2896, 2896, 0)
    matrices[1][9:12] = (-65536, 24576, 8192)
    matrices[2][3:6] = (0, 0, 4096)
    matrices[2][9:12] = (16384, -16384, 32768)
    for i, matrix in enumerate(matrices): put(MATRICES+i*48, *matrix)
    put(MATRIX, *case.get('matrix', IDENTITY))
    put(LIGHT, *case.get('light', (4096, 4096, 4096)))
    # Valid BEGIN/COLOR/VTX16/VTX16/VTX16/END words, opaque to transport.
    put(LIST, 0x23232040, 0, 0x7fff, 0, 0, 0x00001000, 0,
              0x00004123, 0x10000000, 0)
    put(LIST+0x80, 0x00004140, 0)
    return bytes(raw)


def execute(probe, image, case, default, native=False):
    probe.cpu.mem_write(WORK, image)
    probe.cpu.mem_write(probe.names['data_02099f88'], default)
    commands, transports, waits = [], [], []
    fifo_name = 'func_01ffde98' if native else 'func_0205a358'

    def code(cpu, address, size, _):
        if address == probe.names[fifo_name]:
            args = [cpu.reg_read(r) for r in shared.REGS]
            if not native:
                args += [probe.u32(cpu.reg_read(UC_ARM_REG_SP))]
                assert args[3:] == [0, 0], ('unexpected async callback', args)
            channel, source, length = args[:3]
            assert channel == 3 and source in (LIST, LIST+0x80)
            assert length == (40 if source == LIST else 8)
            payload = bytes(cpu.mem_read(source, length))
            transports.append(dict(channel=channel, source=hex(source),
                bytes=length, sha256=base.sha(payload)))
            if not native:
                commands.extend((0, word) for word in struct.unpack('<'+'I'*(length//4), payload))
                cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
        elif address == probe.names['func_02059fa8']:
            assert cpu.reg_read(shared.REGS[0]) == 3
            waits.append(len(transports))
    hook = probe.cpu.hook_add(UC_HOOK_CODE, code)
    writes = None
    if not native:
        def write(cpu, access, address, size, value, _):
            assert size == 4 and not address & 3, (hex(address), size)
            commands.append((0 if address < 0x04000440 else (address-0x04000400)//4,
                             value & 0xffffffff))
        writes = probe.cpu.hook_add(UC_HOOK_MEM_WRITE, write,
                                    begin=0x04000400, end=0x040005c8)
    else:
        probe.call('sm64ds_draw_reset', [])
        assert not any(begin <= 0x04000000 <= end for begin, end, _ in probe.cpu.mem_regions())
    probe.call(FUNCTION, [SELF, case.get('index', 0),
        MATRIX if 'matrix' in case else 0, LIGHT if 'light' in case else 0])
    assert bytes(probe.cpu.mem_read(WORK, 0x10000)) == image, (probe.label, case['name'], 'input/canary mutation')
    assert bytes(probe.cpu.mem_read(probe.names['data_02099f88'], 12)) == default
    if native:
        assert probe.call('sm64ds_draw_ok', []) == 1
        count = probe.call('sm64ds_draw_count', [])
        assert count < 1024
        pointer = probe.call('sm64ds_draw_entries', [])
        commands = [struct.unpack('<II', probe.cpu.mem_read(pointer+i*8, 8)) for i in range(count)]
    probe.cpu.hook_del(hook)
    if writes is not None: probe.cpu.hook_del(writes)
    return commands, transports, waits


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, default=ROOT/'build/3ds/source/ctr/sm64ds-native.elf')
    parser.add_argument('--pack', type=Path, default=ROOT/'build/europe.pak')
    parser.add_argument('--report', type=Path, default=ROOT/'build/native-integration-check/model-billboard-arm-gate.json')
    args = parser.parse_args()
    assert subprocess.check_output(['git', '-C', ROOT/'upstream', 'rev-parse', 'HEAD'], text=True).strip() == shared.PIN
    source = ROOT/'upstream/src/func_02044534.c'
    entry = next(e for e in json.loads((ROOT/'build/native-core/report.json').read_text())['entries'] if e['source'] == 'src/func_02044534.c')
    assert entry['compiled'] and not entry['diagnostics'] and entry['sha256_original'] == base.sha(source.read_bytes())
    assert 'transport-awaited-original-billboard-display-list-through-native-FIFO' in entry['adaptations']
    cart = base.Cartridge(args.pack)
    rom, native = original_machine(cart), shared.Probe('production ARM11', elf=args.elf)
    address, _, image = cart.image(0xffffffff)
    default = image[0x02099f88-address:0x02099f88-address+12]
    assert struct.unpack('<3i', default) == (4096, 4096, 4096)
    cases = [dict(name='empty-entry', entries=0), dict(name='negative-entry', entries=-1),
        dict(name='empty-bones', bones=0), dict(name='normal-default'),
        dict(name='fallback-up', up=(0, 0, 4096)),
        dict(name='threshold-equal', up=(128, 0, 4096)),
        dict(name='threshold-above', up=(129, 0, 4096)),
        dict(name='negative-diagonal', up=(-2896, 2896, 0), shift=3),
        dict(name='signed-light', light=(-4096, 2048, 4096)),
        dict(name='optional-identity', matrix=IDENTITY),
        dict(name='optional-rotated', matrix=(0, 4096, 0, -4096, 0, 0, 0, 0, 4096, 8192, -4096, 0), light=(8192, 2048, 4096)),
        dict(name='three-bones-second-entry', bones=3, index=1),
        dict(name='two-entry-iterations', entries=2, bones=3)]
    results = []
    for case in cases:
        fixture_bytes = fixture(case)
        original = execute(rom, fixture_bytes, case, default)
        actual = execute(native, fixture_bytes, case, default, native=True)
        assert actual == original, (case['name'], original, actual)
        commands, transports, waits = actual
        count = max(case.get('entries', 1), 0)
        bones = case.get('bones', 1)
        assert len(transports) == len(waits) == count
        assert waits == list(range(1, count+1))
        controls = [(port, value) for port, value in commands if port]
        expected = [(0x10, 2), (0x15, 0), (0x10, 1)]
        for block in range(count):
            for bone in range(bones):
                offset = (block*bones+bone)*7
                assert controls[offset:offset+7] == expected+[(0x1b, 1 << (case.get('shift', 1)+12))]*3+[(0x13, bone)]
        assert len(controls) == count*bones*7
        encoded = b''.join(struct.pack('<II', *word) for word in commands)
        results.append(dict(case=case['name'], fixture_sha256=base.sha(fixture_bytes),
            ordered_commands=len(commands), command_sha256=base.sha(encoded),
            transports=transports, awaited_after_submission=waits,
            entire_input_and_canaries_unchanged=True, preserved_registers_and_stack=True))
    report = dict(status='passed', ELF_sha256=base.sha(args.elf.read_bytes()),
        upstream_commit=shared.PIN, cases=results,
        original_function_sha256=base.sha(image[0x02044534-address:0x0204488c-address]),
        source_sha256=base.sha(source.read_bytes()),
        native_adapter_sha256=base.sha((ROOT/'tools/native_core.py').read_bytes()),
        native_object_sha256=base.sha((ROOT/entry['object']).read_bytes()),
        test_sha256=base.sha(Path(__file__).read_bytes()),
        native_DS_MMIO_unmapped=True,
        observed_original_boundary='SDK FIFO submission with callback=0/arg=0, followed by actual original DMA wait',
        native_boundary='Real synchronous FIFO provider, actual native DMA wait and command buffer',
        limits=['Typed default vector is supplied from ROM; full runtime publication is separately gated.',
                'No async IRQ timing equivalence, display-list rasterization, frame, game, physical console or FPS evidence.'])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(dict(status=report['status'], cases=len(results), ELF_sha256=report['ELF_sha256'], report=str(args.report))))


if __name__ == '__main__': main()
