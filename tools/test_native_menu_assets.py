#!/usr/bin/env python3
"""Regression for original OV007 graphics and its deferred display-mode ABI.

Requires the user's ASMP0 asset pack, ndspy and Unicorn. Hashes and addresses
are reported; cartridge assets are never written out. CPU observation seams
test one argument contract, not gameplay, Azahar or physical 3DS performance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import zlib

import ndspy.codeCompression
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC
import build_native_runtime as runtime

ROOT = Path(__file__).resolve().parents[1]
TOOL = runtime.TOOL
# Fixed identities recovered from the supplied ROM, independently of the
# exclusion implementation. Ranges are [start,end), including Nitro headers.
RESOURCES = [
    (0x020dbdbc, 6412, b'NCSC', b'SCRN', 24,
     '8ab027c2a3a93d25fb02b8d338cc85e6a173aa280d92607ad5c85b7b8e77e9af', 5),
    (0x020ea6ec, 33392, b'NCCG', b'CHAR', 20,
     '16eac000c3f8de4135ccd1b78d465aa52bb350fba1323162f4ddf20f2f1bbc83', 4),
    (0x020f295c, 66664, b'NCCG', b'CHAR', 20,
     '6f3298f542ea1e11a4d13b91e2c3d40075c1359aae9b62f046e0232ddf647a49', 15),
]
# These remain real table pointers. Excluding them would prevent the original
# menu from finding its graphics, even if all payload hashes stayed correct.
LEGITIMATE_REFERENCES = [(0x020d7ed8, 0x020dbdbc),
                         (0x020d7e28, 0x020ea6ec),
                         (0x020d7e34, 0x020f295c)]
MODE_SOURCE_SHA256 = 'c2f748ddee430ebdc97e13fed69d0cd71dbeb07fe50ddd800937acf9b6e77a4d'
MODE_FUNCTIONS = ['ARMMathSaveState', 'ARMMathLoadState', 'func_ov007_020bf690',
                  'func_ov007_020bd4e8', 'func_ov007_020bd648', 'func_ov007_020b72a0']
MARKER, STOP, INSTANCE = 0x12340000, 0x400000, 0x220000


def sha(data):
    return hashlib.sha256(data).hexdigest()


def pack_overlay(path):
    raw = path.read_bytes()
    assert raw[:8] == b'SM64PAK1' and raw[16:21] == b'ASMP\0'
    assert struct.unpack_from('<I', raw, 28)[0] == 0x29715dec
    assert zlib.crc32(raw[:40]) == struct.unpack_from('<I', raw, 40)[0]
    count = struct.unpack_from('<I', raw, 12)[0]
    index = raw[64:64+272*count]
    assert zlib.crc32(index) == struct.unpack_from('<I', raw, 36)[0]
    files = {}
    for at in range(0, len(index), 272):
        _, offset, size, crc = struct.unpack_from('<4I', index, at)
        name = index[at+16:at+272].split(b'\0')[0].decode()
        if name not in ('__rom/arm9_overlays.bin', '__rom/file_18.bin'):
            continue
        data = raw[offset:offset+size]
        assert len(data) == size and zlib.crc32(data) == crc
        files[name] = data
    row = struct.unpack_from('<8I', files['__rom/arm9_overlays.bin'], 7*32)
    assert row[0] == 7 and row[1] == 0x020ad660 and row[6] == 18
    overlay = ndspy.codeCompression.decompress(files['__rom/file_18.bin'])
    assert len(overlay) == row[2]
    return overlay, row


def emitted_fixups(path):
    assembly = path.read_text()
    text = assembly.split('sm64ds_runtime_fixups:\n', 1)[1].split(
        'sm64ds_runtime_fixups_end:', 1)[0]
    rows = {}
    for line in text.splitlines():
        if line.startswith('.word '):
            section, offset, expected, replacement = line[6:].split(',')
            rows[(int(section), int(offset))] = (int(expected), replacement)
    return rows


def verify_assets(overlay, base, tables):
    regions, _, _, relocs, _ = runtime.metadata()
    corrected, exclusions = runtime.exclude_opaque_data_relocations(relocs)
    emitted = emitted_fixups(tables)
    resources, omitted = [], []
    for start, size, magic, block_magic, payload_header, digest, count in RESOURCES:
        asset = overlay[start-base:start-base+size]
        assert len(asset) == size and sha(asset) == digest
        assert asset[:4] == magic and asset[4:6] == b'\xff\xfe'
        declared_size, header, block_count = struct.unpack_from('<IHH', asset, 8)
        assert declared_size == size and header == 16
        assert asset[16:20] == block_magic
        block_size = struct.unpack_from('<I', asset, 20)[0]
        payload_start, payload_end = start+16+payload_header, start+16+block_size
        assert start < payload_start < payload_end <= start+size
        cursor = 16
        for _ in range(block_count):
            assert cursor+8 <= size
            section_size = struct.unpack_from('<I', asset, cursor+4)[0]
            assert 8 <= section_size <= size-cursor
            cursor += section_size
        assert cursor == size
        raw = [row for row in relocs if row[0] == 'ov007' and start <= row[1] < start+size]
        assert len(raw) == count
        for module, source, target_module, target in raw:
            assert payload_start <= source and source+4 <= payload_end
            assert struct.unpack_from('<I', overlay, source-base)[0] == target
            assert (module, source, target_module, target) not in corrected
            section, region = runtime.layout.owner(regions, module, source)
            assert (section, source-region['start']) not in emitted
            omitted.append(source)
        # A future pointer-looking pixel word must be reviewed too. Checking
        # only the previously bad offsets could miss a newly added corruption.
        assert not any(m == 'ov007' and start <= source < start+size
                       for m, source, _, _ in corrected)
        resources.append({'start': hex(start), 'size': size, 'sha256': digest,
                          'payload_start': hex(payload_start), 'payload_end': hex(payload_end),
                          'payload_sha256': sha(overlay[payload_start-base:payload_end-base]),
                          'excluded_false_relocations': count})
    assert len(omitted) == 24
    legitimate = []
    for source, target in LEGITIMATE_REFERENCES:
        row = ('ov007', source, 'ov007', target)
        assert row in corrected and row in relocs
        assert struct.unpack_from('<I', overlay, source-base)[0] == target
        section, region = runtime.layout.owner(regions, 'ov007', source)
        assert emitted[(section, source-region['start'])] == (
            target, runtime.layout.expression(regions, 'ov007', target))
        legitimate.append({'source': hex(source), 'target': hex(target)})
    return {'resources': resources, 'excluded_payload_words': len(omitted),
            'legitimate_references_retained': legitimate,
            'reviewed_exclusion_count': len(exclusions)}


def symbols(path):
    output = subprocess.check_output([TOOL+'nm', '--defined-only', str(path)], text=True)
    return {p[2]: int(p[0], 16) for row in output.splitlines()
            if len(p := row.split()) == 3}


def link_observers(obj, temporary, label, stub):
    elf, binary = temporary/(label+'.elf'), temporary/(label+'.bin')
    # The observer harness tests ordinary instruction/argument delivery only.
    # Remove unwinding descriptors from a temporary object copy so this small
    # isolated link does not pull an unrelated C++ unwinder into the test.
    isolated = temporary/(label+'.o')
    subprocess.run([TOOL+'objcopy', '--remove-section=.ARM.exidx*',
                    '--remove-section=.ARM.extab*', obj, isolated], check=True)
    subprocess.run([TOOL+'ld', '-Ttext=0x100000', '-Tdata=0x120000',
                    '-e', 'func_ov007_020b6eb4', isolated, stub, '-o', elf], check=True)
    subprocess.run([TOOL+'objcopy', '-O', 'binary', elf, binary], check=True)
    return binary.read_bytes(), symbols(elf)


def observe_mode(entry, code, base, names, mode, reset_3d):
    cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    cpu.mem_map(base & ~0xffff, ((base & 0xffff)+len(code)+0x1ffff) & ~0xffff)
    cpu.mem_write(base, code)
    cpu.mem_map(INSTANCE, 0x10000)
    cpu.mem_map(STOP, 0x10000)
    for address in (names[name] for name in MODE_FUNCTIONS):
        if not any(begin <= address <= end for begin, end, _ in cpu.mem_regions()):
            cpu.mem_map(address & ~0xfff, 0x1000)
            cpu.mem_write(address, struct.pack('<I', 0xe12fff1e))
    cpu.mem_write(names['data_ov007_02103430'], struct.pack('<I', 1))
    cpu.mem_write(names['data_ov007_0210342c'], struct.pack('<I', INSTANCE))
    cpu.mem_write(INSTANCE+0x18, struct.pack('<2I', reset_3d, mode))
    calls = []
    observers = {names[name]: name for name in MODE_FUNCTIONS}

    def hook(uc, address, size, _):
        name = observers.get(address)
        if not name:
            return
        if name == 'func_ov007_020bd648':
            calls.append(uc.reg_read(UC_ARM_REG_R0))
        if name == 'func_ov007_020bf690':
            uc.reg_write(UC_ARM_REG_R0, MARKER)
        if name == 'func_ov007_020bd4e8':
            uc.reg_write(UC_ARM_REG_R0, MARKER+0x100)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    cpu.hook_add(UC_HOOK_CODE, hook)
    cpu.reg_write(UC_ARM_REG_R0, 0x76543210)
    cpu.reg_write(UC_ARM_REG_SP, STOP+0x8000)
    cpu.reg_write(UC_ARM_REG_LR, STOP)
    cpu.emu_start(entry, STOP, count=10000)
    assert cpu.reg_read(UC_ARM_REG_PC) == STOP and cpu.reg_read(UC_ARM_REG_SP) == STOP+0x8000
    assert cpu.reg_read(UC_ARM_REG_R0) == 1 and len(calls) == 1
    assert bytes(cpu.mem_read(INSTANCE+0x1c, 4)) == b'\0'*4
    return calls[0]


def verify_mode_abi(overlay, row, mode_object):
    original_source = ROOT/'upstream/src/func_ov007_020b6eb4.c'
    assert sha(original_source.read_bytes()) == MODE_SOURCE_SHA256
    rom_names = dict(zip(MODE_FUNCTIONS, [0x0205e988, 0x0205e91c, 0x020bf690,
                                         0x020bd4e8, 0x020bd648, 0x020b72a0]))
    rom_names.update({'data_ov007_02103430': 0x02103430,
                      'data_ov007_0210342c': 0x0210342c})
    rom_image = overlay+bytes(row[3])
    cases = []
    with tempfile.TemporaryDirectory(prefix='sm64ds-menu-assets-') as directory:
        temporary = Path(directory)
        stub_text = '.syntax unified\n.arm\n.text\n'
        stub_text += '\n'.join(f'.global {name}\n{name}: bx lr' for name in MODE_FUNCTIONS)
        stub_text += '''
.data
.balign 4
.global data_ov007_02103430
data_ov007_02103430: .word 1
.global data_ov007_0210342c
data_ov007_0210342c: .word 0x220000
.section .note.GNU-stack,"",%progbits
'''
        stub_source, stub = temporary/'observers.S', temporary/'observers.o'
        stub_source.write_text(stub_text)
        subprocess.run([TOOL+'gcc', *runtime.FLAGS, '-c', stub_source, '-o', stub], check=True)
        # Independently compile the pinned unadapted source as a negative
        # control. The harness must detect its demonstrably lost r0 argument.
        unadapted = temporary/'unadapted.o'
        subprocess.run([TOOL+'gcc', *runtime.FLAGS, '-Os', '-std=gnu11',
                        '-c', original_source, '-o', unadapted], check=True)
        native, native_names = link_observers(mode_object, temporary, 'native', stub)
        broken, broken_names = link_observers(unadapted, temporary, 'unadapted', stub)
        for reset_3d in (0, 1):
            for mode in (1, 2, 3, 4, 5, 6):
                original = observe_mode(0x020b6eb4, rom_image, row[1], rom_names, mode, reset_3d)
                actual = observe_mode(native_names['func_ov007_020b6eb4'], native,
                                      0x100000, native_names, mode, reset_3d)
                control = observe_mode(broken_names['func_ov007_020b6eb4'], broken,
                                       0x100000, broken_names, mode, reset_3d)
                assert original == mode, ('ROM mode changed', mode, original)
                assert control == MARKER+0x100*reset_3d, ('Negative control ceased detecting the defect', control)
                assert actual == mode, ('ARM11 display mode argument lost', mode, actual, reset_3d)
                cases.append({'requested_mode': mode, 'reset_3d_first': bool(reset_3d),
                              'rom_received': original, 'arm11_received': actual,
                              'unadapted_negative_control_received': control})
    return {'mode_object': str(mode_object), 'object_sha256': sha(mode_object.read_bytes()),
            'cases': cases, 'negative_control_reproduces_original_bug': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack', type=Path, default=ROOT/'build/europe.pak')
    parser.add_argument('--tables', type=Path, default=ROOT/'build/native-runtime/native_tables.S')
    parser.add_argument('--mode-object', type=Path,
                        default=ROOT/'build/native-runtime/scene-video/func_ov007_020b6eb4.o')
    parser.add_argument('--report', type=Path, default=ROOT/'build/native-runtime/menu-assets-test.json')
    args = parser.parse_args()
    overlay, row = pack_overlay(args.pack)
    asset_result = verify_assets(overlay, row[1], args.tables)
    abi_result = verify_mode_abi(overlay, row, args.mode_object)
    report = {'status': 'passed', 'scope': 'Host pack and isolated ARM11 instruction regression',
              'upstream_commit': '6230b2996636e3fd0e03cb178161c94cb4b73cc9',
              'physical_hardware': False, 'playability_verified': False,
              'azahar_exercised': False, 'cartridge_bytes_written_to_disk': False,
              'assets': asset_result, 'mode_abi': abi_result}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({'status': report['status'], 'report': str(args.report),
                      'resources': len(asset_result['resources']),
                      'excluded_payload_words': asset_result['excluded_payload_words'],
                      'legitimate_references': len(asset_result['legitimate_references_retained']),
                      'mode_abi_cases': len(abi_result['cases'])}, indent=2))


if __name__ == '__main__':
    main()
