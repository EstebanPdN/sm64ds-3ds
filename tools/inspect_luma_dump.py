#!/usr/bin/env python3
"""Decode a local Luma ARM11 dump and verify its code window against an ELF.

Private evidence stays on disk. This is a crash snapshot, not a full RAM dump.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from elftools.elf.elffile import ELFFile
from devkitpro_tools import arm_tool

REGISTERS = [*('r'+str(i) for i in range(13)), 'sp', 'lr', 'pc', 'cpsr',
             'dfsr', 'ifsr', 'far', 'fpexc', 'fpinst', 'fpinst2']


def inspect(dump, elf):
    data = Path(dump).read_bytes()
    if len(data) < 40:
        raise ValueError('Truncated Luma header')
    m0, m1, version, cpu, kind, size, reg_size, code_size, stack_size, extra_size = struct.unpack_from('<10I', data)
    if (m0, m1) != (0xdeadc0de, 0xdeadcafe) or cpu & 0xffff != 11:
        raise ValueError('Expected a Luma ARM11 dump')
    if size != len(data) or size != 40+reg_size+code_size+stack_size+extra_size or reg_size != 4*len(REGISTERS):
        raise ValueError('Inconsistent dump sizes / unsupported register layout')
    registers = dict(zip(REGISTERS, struct.unpack_from('<23I', data, 40)))
    code_offset = 40 + reg_size
    code = data[code_offset:code_offset+code_size]
    # Luma's code window ends immediately after the faulting instruction.
    # ARM state is required here; Thumb windows need different alignment.
    if registers['cpsr'] & 0x20:
        raise ValueError('Thumb code-window verification not implemented')
    start = registers['pc'] + 4 - code_size
    code_matches = False
    with Path(elf).open('rb') as stream:
        executable = ELFFile(stream)
        for segment in executable.iter_segments():
            base, length = segment['p_vaddr'], segment['p_filesz']
            if base <= start and start+code_size <= base+length:
                code_matches = segment.data()[start-base:start-base+code_size] == code
                break
    stack = data[code_offset+code_size:code_offset+code_size+stack_size]
    addresses = [registers['pc'], registers['lr']]
    with Path(elf).open('rb') as stream:
        executable = ELFFile(stream)
        ranges = [(s['p_vaddr'], s['p_vaddr']+s['p_filesz']) for s in executable.iter_segments() if s['p_flags'] & 1]
    for (address,) in struct.iter_unpack('<I', stack[:len(stack)//4*4]):
        if address not in addresses and any(a <= address < b for a, b in ranges):
            addresses.append(address)
    symbols = subprocess.check_output([arm_tool('addr2line'), '-C', '-f', '-e', str(elf),
                                      *[hex(a) for a in addresses]], text=True).splitlines()
    extra = data[-extra_size:] if extra_size else b''
    return dict(dump_sha256=hashlib.sha256(data).hexdigest(), bytes=len(data),
                format_version=version, cpu=cpu, exception=kind,
                exception_name=['FIQ','undefined instruction','prefetch abort','data abort'][kind] if kind<4 else 'unknown',
                registers={k:f'0x{v:08x}' for k,v in registers.items()},
                code_window_start=hex(start), code_window_matches_ELF=code_matches,
                ELF_sha256=hashlib.sha256(Path(elf).read_bytes()).hexdigest(),
                symbols=[dict(address=hex(a), function=symbols[i*2], source=symbols[i*2+1]) for i,a in enumerate(addresses)],
                process_name=extra[:8].rstrip(b'\0').decode('ascii', errors='replace'),
                title_id=f'{struct.unpack_from("<Q",extra,8)[0]:016X}' if len(extra)>=16 else None,
                limits='Stack words are candidate return addresses, not an unwound call stack. No full RAM/VRAM in this dump.')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dump', type=Path)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    args=parser.parse_args()
    result=inspect(args.dump,args.elf)
    rendered=json.dumps(result,indent=2)+'\n'
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(rendered)
    print(rendered)
    if not result['code_window_matches_ELF']:
        raise SystemExit('Code window differs from ELF; symbol names cannot identify this build reliably')


if __name__=='__main__':main()
