#!/usr/bin/env python3
"""Verify unchanged tested providers in a new general ELF, without CPU replay."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]
TOOL='/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
PROVIDERS=('LenVec3','func_02053644','func_02053380','func_020518dc',
    'func_02051f1c','func_02051e60','_ZN4cstd3modEii','_ZN4cstd4sqrtEy',
    '_ZN6sm64ds4math9game_sqrtEy','_ZN6sm64ds4math12integer_sqrtEy',
    '_ZN6sm64ds4math9remainderEll')
AUDIO={'func_020518dc','func_02051f1c','func_02051e60'}

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def string_at(elf,address):
    raw=elf.read_bytes()
    assert raw[:7]==b'\x7fELF\x01\x01\x01'
    phoff=struct.unpack_from('<I',raw,28)[0]
    phsize,phnum=struct.unpack_from('<HH',raw,42)
    assert phsize==32
    for index in range(phnum):
        kind,offset,base,_,filesize,_,_,_=struct.unpack_from('<8I',raw,phoff+index*phsize)
        if kind==1 and base<=address<base+filesize:
            start=offset+address-base
            end=raw.index(0,start,min(start+1024,offset+filesize))
            value=raw[start:end]
            assert value and all(32<=byte<127 for byte in value)
            return value.decode('ascii')
    raise ValueError('Diagnostic string pointer is outside file-backed ELF segments')

def normalized(elf,symbol):
    source=subprocess.check_output([TOOL+'objdump','-d','--disassemble='+symbol,str(elf)],text=True)
    instructions=[]
    for line in source.splitlines():
        match=re.match(r'\s*[0-9a-f]+:\s+[0-9a-f]+\s+(.+)',line)
        if not match:continue
        instruction=match[1].split('@',1)[0].strip()
        instruction=re.sub(r'\s+',' ',instruction)
        instruction=re.sub(r'\b[0-9a-f]+ (?=<)', '',instruction)
        literal=re.fullmatch(r'\.word 0x([0-9a-f]+)',instruction)
        if symbol in AUDIO and literal:
            instruction='.cstring '+string_at(elf,int(literal[1],16))
        instructions.append(instruction)
    assert instructions,'Provider has no instructions: '+symbol
    return instructions

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline',type=Path,required=True)
    parser.add_argument('--elf',type=Path,default=ROOT/'build/3ds/source/ctr/sm64ds-native.elf')
    parser.add_argument('--report',type=Path,default=ROOT/'build/native-integration-check/provider-reuse.json')
    args=parser.parse_args()
    symbols=subprocess.check_output([TOOL+'nm','--defined-only',str(args.elf)],text=True).splitlines()
    checks={}
    for symbol in PROVIDERS:
        definitions=[line for line in symbols if line.split()[-1]==symbol]
        assert len(definitions)==1,(symbol,definitions)
        old,new=normalized(args.baseline,symbol),normalized(args.elf,symbol)
        assert old==new,('Tested provider instructions changed',symbol,old,new)
        checks[symbol]={'single_definition':True,'definition':definitions[0],
            'instructions_equal_to_verified_baseline':True,
            'normalized_sha256':hashlib.sha256('\n'.join(new).encode()).hexdigest()}
    report={'status':'passed','scope':'Read-only provider linkage and normalized instruction reuse; no CPU/emulator execution',
        'elf':str(args.elf),'elf_sha256':digest(args.elf),'baseline':str(args.baseline),
        'baseline_elf_sha256':digest(args.baseline),
        'normalization':'Remove addresses/encodings/comments; retain operands and resolved branch symbols. Audio literal string pointers compare all referenced bytes.',
        'providers':checks}
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'status':'passed','providers':len(checks),'elf_sha256':report['elf_sha256'],
        'report':str(args.report),'CPU_or_Azahar_executed':False},indent=2))

if __name__=='__main__':main()
