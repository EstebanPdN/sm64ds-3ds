#!/usr/bin/env python3
"""Regress the native runtime's publication of the original DSMT callbacks.

Executes the production ELF's ARM11 runtime loader, checked constant/callback
initializers, resolver and callback veneers in Unicorn. Controlled seams supply
independently verified cartridge images/catalog and a host-managed C++ heap.
This is an instruction/data regression, not game, Azahar or physical 3DS proof.
Cartridge bytes remain in memory; reports contain addresses and hashes only.
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
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
    UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR,
    UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC)

ROOT = Path(__file__).resolve().parents[1]
TOOL = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
ARENA, ARENA_SIZE, STACK, STOP = 0x10000000, 0x4000000, 0x18000000, 0x18010000
CALLBACKS = ['func_ov007_020ccab4', 'func_ov007_020cca98',
             'func_ov007_020cca80', 'func_ov007_020cca74',
             'func_ov007_020cca68', 'func_ov007_020cc600',
             'func_ov007_020c3e4c', 'func_ov007_020c3e64']
ORIGINAL_CALLBACKS = [0x020ccab4, 0x020cca98, 0x020cca80, 0x020cca74,
                      0x020cca68, 0x020cc600, 0x020c3e4c, 0x020c3e64]
SAVED = [UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
         UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def symbols(path):
    text = subprocess.check_output([TOOL+'nm', '--defined-only', path], text=True)
    return {p[2]: int(p[0], 16) for row in text.splitlines()
            if len(p := row.split()) == 3}


def layout_contract():
    # Verify the actual target compiler's object sizes/offsets before injecting
    # any test-only C++ object. No game declaration or production object changes.
    source = '''#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include <cstddef>
#define private public
#include "sm64ds/game_data.hpp"
#undef private
using namespace sm64ds;
extern "C" const uint32_t contract[] = {
 sizeof(std::string), sizeof(std::vector<uint8_t>), sizeof(AssetEntry),
 sizeof(RomInfo), sizeof(AssetPack), offsetof(AssetPack, info_),
 offsetof(RomInfo, revision), offsetof(RomInfo, size), offsetof(RomInfo, crc),
 offsetof(RomInfo, files), offsetof(AssetEntry, path),
 sizeof(game_data::OverlayImage), offsetof(game_data::OverlayImage, bytes),
 sizeof(game_data::HandleTable), offsetof(game_data::HandleTable, asset_indices)};
'''
    with tempfile.TemporaryDirectory(prefix='sm64ds-callback-layout-') as directory:
        temp = Path(directory)
        (temp/'contract.cpp').write_text(source)
        subprocess.run([TOOL+'g++', '-march=armv6k', '-mfloat-abi=hard', '-Os',
                        '-I'+str(ROOT/'include'), '-Wa,--noexecstack', '-c',
                        temp/'contract.cpp', '-o', temp/'contract.o'], check=True)
        subprocess.run([TOOL+'objcopy', '--dump-section',
                        '.rodata='+str(temp/'contract.bin'), temp/'contract.o'], check=True)
        values = struct.unpack('<15I', (temp/'contract.bin').read_bytes())
    assert values == (24, 12, 40, 48, 52, 4, 24, 28, 32, 36, 16, 24, 12, 16, 4), values
    return list(values)


class Cartridge:
    def __init__(self, path):
        self.raw = path.read_bytes()
        assert self.raw[:8] == b'SM64PAK1' and self.raw[16:21] == b'ASMP\0'
        assert struct.unpack_from('<I', self.raw, 8)[0] == 1
        assert struct.unpack_from('<I', self.raw, 28)[0] == 0x29715dec
        assert struct.unpack_from('<I', self.raw, 32)[0] == len(self.raw)
        assert zlib.crc32(self.raw[:40]) == struct.unpack_from('<I', self.raw, 40)[0]
        self.revision = self.raw[20]
        self.rom_size = struct.unpack_from('<I', self.raw, 24)[0]
        count = struct.unpack_from('<I', self.raw, 12)[0]
        index = self.raw[64:64+272*count]
        assert len(index) == 272*count
        assert zlib.crc32(index) == struct.unpack_from('<I', self.raw, 36)[0]
        self.entries = []
        for at in range(0, len(index), 272):
            values = struct.unpack_from('<4I', index, at)
            name = index[at+16:at+272].split(b'\0')[0].decode()
            self.entries.append((*values, name))
        self.by_name = {e[4]: i for i, e in enumerate(self.entries)}
        assert len(self.by_name) == count
        table = self.file('__rom/arm9_overlays.bin')
        assert len(table) % 32 == 0
        self.overlays = {r[0]: r for at in range(0, len(table), 32)
                         for r in [struct.unpack_from('<8I', table, at)]}
        assert len(self.overlays) == len(table)//32
        self.images = {}

    def file(self, name):
        _, offset, size, crc, _ = self.entries[self.by_name[name]]
        result = self.raw[offset:offset+size]
        assert len(result) == size and zlib.crc32(result) == crc, name
        return result

    def image(self, module):
        if module not in self.images:
            if module == 0xffffffff:
                header, packed = self.file('__rom/header.bin'), self.file('__rom/arm9.bin')
                base = struct.unpack_from('<I', header, 0x28)[0]
                assert len(packed) == struct.unpack_from('<I', header, 0x2c)[0]
                image = ndspy.codeCompression.decompress(packed)
                assert len(image) == len(packed)+struct.unpack_from('<I', packed, len(packed)-4)[0]
                bss = 0
            else:
                row = self.overlays[module]
                base, expected, bss, file_id, flags = row[1], row[2], row[3], row[6], row[7]
                entry = next(e for e in self.entries if e[0] == file_id)
                packed = self.file(entry[4])
                if flags & (1 << 24):
                    assert flags & 0xffffff == len(packed)
                    image = ndspy.codeCompression.decompress(packed)
                else:
                    image = packed
                assert len(image) == expected
            self.images[module] = (base, bss, image)
        return self.images[module]

    def handles(self):
        # Reconstruct the unique catalog using the production structural rule.
        # This observer supplies its result; no synthetic IDs enter the runtime.
        base, _, image = self.image(0)
        names = {e[4]: i for i, e in enumerate(self.entries) if not e[4].startswith('__rom/')}
        runs, active, start = [], [], None
        for offset in range(0, len(image)-3, 4):
            pointer = struct.unpack_from('<I', image, offset)[0]
            index = None
            if base <= pointer < base+len(image):
                text = image[pointer-base:pointer-base+256].split(b'\0', 1)
                if len(text) == 2:
                    index = names.get(text[0].decode('utf-8', errors='replace'))
            if index is None:
                if len(active) >= 1024:
                    runs.append((start, active))
                active, start = [], None
            else:
                if not active:
                    start = base+offset
                active.append(index)
        if len(active) >= 1024:
            runs.append((start, active))
        assert len(runs) == 1 and len(runs[0][1]) == 2058
        assert len(set(runs[0][1])) == 2058
        assert all(self.entries[i][0] < 0x8000 for i in runs[0][1])
        return runs[0]


class Machine:
    def __init__(self, elf, cartridge):
        self.names, self.cart = symbols(elf), cartridge
        self.cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        self.cpu.reg_write(UC_ARM_REG_C1_C0_2, 0xf << 20)
        self.cpu.reg_write(UC_ARM_REG_FPEXC, 1 << 30)
        raw = elf.read_bytes()
        assert raw[:7] == b'\x7fELF\x01\x01\x01'
        offset, size, count = struct.unpack_from('<I', raw, 28)[0], *struct.unpack_from('<HH', raw, 42)
        loads = [struct.unpack_from('<8I', raw, offset+i*size) for i in range(count)]
        loads = [p for p in loads if p[0] == 1]
        begin = min(p[2] for p in loads) & ~0xfff
        end = (max(p[2]+p[5] for p in loads)+0xfff) & ~0xfff
        self.cpu.mem_map(begin, end-begin)
        for _, off, address, _, filesz, _, _, _ in loads:
            self.cpu.mem_write(address, raw[off:off+filesz])
        self.cpu.mem_map(ARENA, ARENA_SIZE)
        self.cpu.mem_map(STACK, 0x20000)
        self.next = ARENA+0x1000
        self.seams, self.events, self.image_addresses = {}, [], {}
        self.pack = self.allocate(52)
        self.error = self.allocate(24)
        self.string(self.error, b'')
        self.string(self.pack+4, b'ASMP')
        self.write(self.pack+28, bytes([cartridge.revision]))
        self.words(self.pack+32, cartridge.rom_size, 0x29715dec)
        entries = self.allocate(40*len(cartridge.entries))
        for i, entry in enumerate(cartridge.entries):
            self.words(entries+i*40, *entry[:4])
            self.string(entries+i*40+16, entry[4].encode())
        self.vector(self.pack+40, entries, 40*len(cartridge.entries))
        self.arm_loader = self.find('_ZN6sm64ds9game_data9load_arm9')
        self.overlay_loader = self.find('_ZN6sm64ds9game_data12load_overlay')
        self.handle_loader = self.find('_ZN6sm64ds9game_data12load_handles')
        self.seams[self.arm_loader] = lambda: self.load_image(0xffffffff, self.reg(1))
        self.seams[self.overlay_loader] = lambda: self.load_image(self.reg(1), self.reg(2))
        self.seams[self.handle_loader] = self.load_handles
        self.seams[self.names['_Znwj']] = self.new
        for name in ('_ZdlPv', '_ZdlPvj'):
            self.seams[self.names[name]] = lambda: None
        self.cpu.hook_add(UC_HOOK_CODE, self.hook)

    def find(self, prefix):
        result = [value for name, value in self.names.items() if name.startswith(prefix)]
        assert len(result) == 1, (prefix, result)
        return result[0]

    def write(self, address, data):
        self.cpu.mem_write(address, bytes(data))

    def words(self, address, *values):
        self.write(address, struct.pack('<'+'I'*len(values), *values))

    def read_words(self, address, count=1):
        return struct.unpack('<'+'I'*count, self.cpu.mem_read(address, 4*count))

    def allocate(self, length):
        address = self.next
        self.next = (address+max(length, 1)+15) & ~15
        assert self.next <= ARENA+ARENA_SIZE, 'Observer C++ heap exhausted'
        return address

    def reg(self, index):
        return self.cpu.reg_read((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)[index])

    def string(self, address, data):
        if len(data) <= 15:
            self.words(address, address+8, len(data))
            self.write(address+8, data+b'\0')
        else:
            pointer = self.allocate(len(data)+1)
            self.words(address, pointer, len(data), len(data))
            self.write(pointer, data+b'\0')

    def vector(self, address, data, length):
        self.words(address, data, data+length, data+length)

    def new(self):
        self.cpu.reg_write(UC_ARM_REG_R0, self.allocate(self.reg(0)))

    def load_image(self, module, result):
        base, bss, image = self.cart.image(module)
        if module not in self.image_addresses:
            pointer = self.allocate(len(image))
            self.write(pointer, image)
            self.image_addresses[module] = pointer
        self.words(result, module, base, bss)
        self.vector(result+12, self.image_addresses[module], len(image))
        self.cpu.reg_write(UC_ARM_REG_R0, 1)
        self.events.append(('image', module))

    def load_handles(self):
        original, indices = self.cart.handles()
        pointer = self.allocate(4*len(indices))
        self.words(pointer, *indices)
        self.words(self.reg(1), original)
        self.vector(self.reg(1)+4, pointer, 4*len(indices))
        self.cpu.reg_write(UC_ARM_REG_R0, 1)
        self.events.append(('verified_handle_catalog', len(indices)))

    def hook(self, uc, address, size, _):
        seam = self.seams.get(address)
        if seam is not None:
            seam()
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
        elif address == self.names.get('sm64ds_prepare_game_data'):
            self.events.append(('production_publication_initializer', address))

    def call(self, name, *args, count=30000000):
        self.cpu.reg_write(UC_ARM_REG_CPSR, 0x10)
        for i, value in enumerate((*args, 0, 0, 0, 0)[:4]):
            self.cpu.reg_write((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)[i], value)
        self.cpu.reg_write(UC_ARM_REG_SP, STACK+0xf000)
        self.cpu.reg_write(UC_ARM_REG_LR, STOP)
        for i, reg in enumerate(SAVED):
            self.cpu.reg_write(reg, 0x12340000+i)
        self.cpu.emu_start(self.names[name], STOP, count=count)
        assert self.cpu.reg_read(UC_ARM_REG_PC) == STOP, ('Instruction bound', name, hex(self.cpu.reg_read(UC_ARM_REG_PC)))
        assert self.cpu.reg_read(UC_ARM_REG_SP) == STACK+0xf000
        assert all(self.cpu.reg_read(reg) == 0x12340000+i for i, reg in enumerate(SAVED))
        return self.reg(0)


def verify_publication(machine):
    names, cart = machine.names, machine.cart
    entry = next(name for name in names if name.startswith('_ZN6sm64ds19native_runtime_load'))
    assert 'sm64ds_prepare_game_data' in names, 'Production publication initializer was discarded by linker'
    globals_ = ['data_ov007_02103290', 'data_ov007_02103254', 'data_ov007_02103258',
                'data_ov007_02103260', 'data_02082214', 'data_ov007_0210325c']
    assert all(machine.read_words(names[name])[0] == 0 for name in globals_)
    assert machine.call(entry, machine.pack, machine.error) == 1, 'Native runtime rejected verified ROM'
    assert sum(event[0] == 'production_publication_initializer' for event in machine.events) == 1
    publication = next(i for i, event in enumerate(machine.events) if event[0] == 'production_publication_initializer')
    assert any(event[0] == 'verified_handle_catalog' for event in machine.events[:publication])
    expected = [names[name] for name in CALLBACKS]
    table = list(machine.read_words(names['data_ov007_02103290'], 6))
    defaults = [machine.read_words(names[name])[0] for name in ('data_ov007_02103254', 'data_ov007_02103258')]
    assert table+defaults == expected
    assert all(value and value < 0x02000000 for value in expected)
    assert machine.read_words(names['data_ov007_02103260'])[0] == 0xffffffff
    main_base, _, arm9 = cart.image(0xffffffff)
    trig = arm9[0x02082214-main_base:0x02082214-main_base+16384]
    assert len(trig) == 16384 and zlib.crc32(trig) == 0xee067d57
    assert bytes(machine.cpu.mem_read(names['data_02082214'], len(trig))) == trig
    menu_base, _, menu = cart.image(7)
    scale = menu[0x0210325c-menu_base:0x0210325c-menu_base+4]
    assert bytes(machine.cpu.mem_read(names['data_ov007_0210325c'], 4)) == scale
    assert zlib.crc32(scale) == 0x3d627c6c
    original_table = list(struct.unpack_from('<6I', menu, 0x02103290-menu_base))
    original_defaults = list(struct.unpack_from('<2I', menu, 0x02103254-menu_base))
    assert original_table+original_defaults == ORIGINAL_CALLBACKS
    # Loading twice must fail; the published callback table remains intact.
    assert machine.call(entry, machine.pack, machine.error) == 0
    assert list(machine.read_words(names['data_ov007_02103290'], 6))+defaults == expected
    return {'callback_table': [{'symbol': name, 'original': hex(original), 'native': hex(native)}
                              for name, original, native in zip(CALLBACKS, ORIGINAL_CALLBACKS, expected)],
            'state': -1, 'trigonometry_bytes': len(trig), 'trigonometry_sha256': sha(trig),
            'scale': struct.unpack('<i', scale)[0], 'scale_sha256': sha(scale),
            'production_initializer_calls': 1, 'catalog_checked_before_publication': True,
            'reinitialization_rejected': True, 'preview_owned_globals_reviewed': globals_}


def verify_veneers(machine):
    calls, cases = [], []
    names = machine.names
    allocation = ARENA+ARENA_SIZE-0x1000

    def observe(label, args):
        actual = [machine.reg(i) for i in range(len(args))]
        assert actual == args, (label, actual, args)
        calls.append(label)
        machine.cpu.reg_write(UC_ARM_REG_R0, allocation)

    for size in (1, 128, 0x188, 4096):
        machine.seams[names['func_02059170']] = lambda n=size: observe('default_allocate', [0, 0xffffffff, n])
        result = machine.call('func_ov007_020c3df4', 0xdeadbeef, size)
        assert result == allocation
        cases.append({'path': 'default allocate', 'size': size, 'return_pointer_preserved': True})
    machine.seams[names['func_020590fc']] = lambda: observe('default_free', [0, 0xffffffff, allocation])
    machine.call('func_ov007_020c3d1c', allocation)
    cases.append({'path': 'default free', 'pointer_preserved': True})
    # b7138 replaces defaults with the original six-word interface's first two
    # callbacks. Exercise the real setters and tail-call leaves in that state.
    machine.call('func_ov007_020c3e3c', names[CALLBACKS[0]])
    machine.call('func_ov007_020c3e2c', names[CALLBACKS[1]])
    for size in (1, 128, 0x188, 4096):
        machine.seams[names[CALLBACKS[0]]] = lambda n=size: observe('menu_allocate', [n])
        assert machine.call('func_ov007_020c3df4', 0xdeadbeef, size) == allocation
        cases.append({'path': 'menu allocate', 'size': size, 'return_pointer_preserved': True})
    machine.seams[names[CALLBACKS[1]]] = lambda: observe('menu_free', [allocation])
    machine.call('func_ov007_020c3d1c', allocation)
    cases.append({'path': 'menu free', 'pointer_preserved': True})
    assert len(calls) == len(cases) == 10
    return {'cases': cases, 'controlled_allocation_observers': True,
            'real_veneer_and_setter_instructions': True, 'callee_saved_registers_preserved': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack', type=Path, default=ROOT/'build/europe.pak')
    parser.add_argument('--elf', type=Path, default=ROOT/'build/3ds/source/ctr/sm64ds-native.elf')
    parser.add_argument('--report', type=Path, default=ROOT/'build/native-runtime/menu-callbacks-test.json')
    args = parser.parse_args()
    contract = layout_contract()
    cart = Cartridge(args.pack)
    machine = Machine(args.elf, cart)
    publication = verify_publication(machine)
    veneers = verify_veneers(machine)
    report = {'status': 'passed', 'scope': 'Production ARM11 runtime publication and callback ABI in a controlled host CPU harness',
              'elf': str(args.elf), 'elf_sha256': sha(args.elf.read_bytes()),
              'rom_code': 'ASMP', 'rom_revision': 0, 'rom_crc32': '29715dec',
              'target_cpp_layout_verified': contract, 'publication': publication,
              'veneer_abi': veneers, 'image_loader_calls': sum(x[0] == 'image' for x in machine.events),
              'seams': ['verified cartridge image loading', 'verified structural handle catalog',
                        'host-managed C++ allocation/deallocation', 'allocation/free observation for veneers'],
              'azahar_tested': False, 'physical_3ds_tested': False,
              'gameplay_or_performance_established': False, 'cartridge_bytes_written_to_disk': False}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({'status': 'passed', 'report': str(args.report),
                      'callbacks_published': 8, 'veneer_cases': len(veneers['cases']),
                      'elf_sha256': report['elf_sha256']}, indent=2))


if __name__ == '__main__':
    main()
