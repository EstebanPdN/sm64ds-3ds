#!/usr/bin/env python3
"""Host streaming/format/I/O checks plus isolated ARM compile and table coverage.

Does not launch a game, an emulator, or an integrated build. Fixtures are local
synthetic bytes; the linked runtime table is inspected read-only for coverage.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/native-integration-check/snapshot-tests'
TOOL = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-'


def run(args):
    result = subprocess.run([str(a) for a in args], cwd=ROOT,
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f'{args[0]} exited {result.returncode}:\n{result.stdout}{result.stderr}')
    return result.stdout


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def elf_range(data, address, length):
    assert data[:6] == b'\x7fELF\x01\x01'
    offset = struct.unpack_from('<I', data, 28)[0]
    size, count = struct.unpack_from('<HH', data, 42)
    for i in range(count):
        kind, file_offset, base, _, file_size, *_ = struct.unpack_from(
            '<8I', data, offset + i*size)
        if kind == 1 and address >= base and address-base <= file_size-length:
            start = file_offset+address-base
            return data[start:start+length]
    raise AssertionError('runtime table is not file-backed')


def main():
    global OUT
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-directory',type=Path,default=OUT)
    OUT=parser.parse_args().output_directory.resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    # Reruns reuse only this synthetic test directory; no run artifacts touched.
    for path in OUT.glob('*.bin'):
        path.unlink()
    for path in OUT.glob('*.json'):
        path.unlink()
    host = OUT / 'snapshot-host'
    run(['c++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
         '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
         '-I', ROOT/'include', ROOT/'tests/test_native_snapshot.cpp', '-o', host])
    checks = json.loads(run([host, OUT]))
    run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', ROOT/'include',
         '-c', ROOT/'source/game_port/native_snapshot.cpp', '-o', OUT/'snapshot-standalone-host.o'])
    binary = (OUT/'fixture.ram.bin').read_bytes()
    index = json.loads((OUT/'fixture.ram.json').read_text())
    assert index['phase'] == 'title "state"\n11'
    assert index['tick'] == 180 and index['presentation'] == 360
    assert index['io_status'] == 'complete' and index['binary_bytes'] == len(binary)
    assert len(index['spans']) == 5 and len(index['views']) == 230
    assert index['runtime_section_count'] == 226
    assert index['stack_probe_kind'] == 'local variable in snapshot call; not original fault PC'
    assert index['stack_range_kind'] == 'OS readable mapping containing probe; not exclusive stack bounds'
    assert index['stack_available'] and index['views'][-2]['name'] == 'mapped-stack-residency-region'
    assert index['thread_tls_available'] and index['thread_tls_address'] == 0x1ff82000
    assert index['views'][-1]['name'] == 'calling-thread-tls' and index['views'][-1]['size'] == 512
    assert index['views'][-1]['address'] == index['thread_tls_address']
    for i, span in enumerate(index['spans']):
        assert struct.unpack_from('<4I', binary, 32+i*16) == (
            span['address'], span['size'], span['file_offset'], i)
        payload = binary[span['file_offset']:span['file_offset']+span['size']]
        assert payload == bytes((j*37+i*17)&255 for j in range(span['size']))
    for i, view in enumerate(index['views']):
        assert struct.unpack_from('<7I', binary, 32+len(index['spans'])*16+i*28) == (
            view['address'], view['size'], view['file_offset'], view['span'],
            view['module'], view['original_address'], view['flags'])
        span = index['spans'][view['span']]
        assert view['file_offset'] == span['file_offset']+view['address']-span['address']
        assert view['file_offset']+view['size'] <= span['file_offset']+span['size']
        assert view['zero_fill_section'] == bool(view['flags'] & 1)
    flags = (ROOT/'build/3ds/source/ctr/CMakeFiles/sm64ds_native_services.dir/flags.make').read_text()
    args = []
    for key in ['CXX_DEFINES', 'CXX_INCLUDES', 'CXX_FLAGS']:
        args.extend(shlex.split(re.search(r'^'+key+r' = (.*)$', flags, re.M)[1]))
    obj = OUT/'native-snapshot.o'
    run([TOOL+'g++', *args, '-Wall', '-Wextra', '-Werror', '-c',
         ROOT/'source/game_port/native_snapshot.cpp', '-o', obj])
    elf = ROOT/'build/3ds/source/ctr/sm64ds-native.elf'
    symbols = {name: int(address, 16) for address, kind, name in
               (line.split() for line in run([TOOL+'nm', '--defined-only', elf]).splitlines()
                if len(line.split()) == 3)}
    sections = run([TOOL+'objdump', '-h', elf])
    row = re.search(r'^\s*\d+\s+\.data\s+([0-9a-f]+)\s+([0-9a-f]+)', sections, re.M)
    rw_start, rw_end = int(row[2], 16), symbols['__bss_end__']
    table_start, table_end = symbols['sm64ds_runtime_sections'], symbols['sm64ds_runtime_sections_end']
    assert (table_end-table_start) % 20 == 0
    rows = list(struct.iter_unpack('<5I', elf_range(elf.read_bytes(), table_start, table_end-table_start)))
    assert len(rows) == 226
    for address, module, original, size, zero in rows:
        assert size and address >= rw_start and size <= rw_end-rw_start
        assert address-rw_start <= rw_end-rw_start-size
        assert zero in (0, 1)
    report = {'status': 'passed', 'scope': 'Host fixtures with ASan/UBSan, strict ARMv6K object compilation and read-only ELF table coverage',
              'host_checks': checks, 'json_and_LE32_descriptor_checks': True,
              'arm_compile_diagnostics': 'none', 'arm_object_sha256': sha(obj),
              'elf_inspected': str(elf), 'elf_sha256': sha(elf),
              'runtime_section_ABI_bytes': 20, 'runtime_sections': len(rows),
              'runtime_sections_inside_ELF_RW': True,
              'ELF_RW_bounds': {'address': rw_start, 'size': rw_end-rw_start},
              'source_sha256': {str(p.relative_to(ROOT)): sha(p) for p in [ROOT/'include/sm64ds/native_snapshot.hpp',
                  ROOT/'source/game_port/native_snapshot.cpp', ROOT/'tests/test_native_snapshot.cpp', Path(__file__).resolve()]},
              'general_build_executed': False, 'Azahar_executed': False, 'physical_hardware': False}
    destination = OUT/'report.json'
    destination.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({'status': report['status'], 'report': str(destination), 'host_checks': checks, 'runtime_sections': len(rows)}))


if __name__ == '__main__':
    main()
