#!/usr/bin/env python3
"""Compare native ARM11 rotation leaves with the supplied ROM's Thumb leaves.

This is a bounded CPU test, not execution of a DS game or a 3DS application.
Only the three pure-memory original leaf functions are executable as oracles.
"""
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
import zlib

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8,
    UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_SP,
    UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR)

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'build/game-data/python'))
import ndspy.rom
import ndspy.codeCompression

OUT=ROOT/'build/native-rotations'
TOOL='/opt/devkitpro/devkitARM/bin/arm-none-eabi-'
NAMES=('func_02052800','func_02052820','func_0205283c')
OUT.mkdir(exist_ok=True)
obj=ROOT/'build/3ds/source/ctr/CMakeFiles/sm64ds_game_primitives.dir/game_primitives.o'
elf_path=OUT/'rotations.elf'
subprocess.run([TOOL+'g++','-march=armv6k','-mfloat-abi=hard','-mtp=soft',
    '-nostartfiles','-Wl,--gc-sections,-z,noexecstack,-Ttext=0x100000,-e,'+NAMES[0],
    '-Wl,-u,'+NAMES[1]+',-u,'+NAMES[2],str(obj),'-o',str(elf_path)],check=True)
symbols={}
for line in subprocess.check_output([TOOL+'nm','--defined-only',str(elf_path)],text=True).splitlines():
    parts=line.split()
    if len(parts)==3: symbols[parts[2]]=int(parts[0],16)
cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM)
elf=elf_path.read_bytes()
phoff=struct.unpack_from('<I',elf,28)[0]
phsize,phnum=struct.unpack_from('<HH',elf,42)
pages=set()
for i in range(phnum):
    typ,off,va,pa,filesz,memsz,flags,align=struct.unpack_from('<8I',elf,phoff+i*phsize)
    if typ!=1 or not memsz: continue
    for page in range(va&~4095,(va+memsz+4095)&~4095,4096):
        if page not in pages: cpu.mem_map(page,4096,7);pages.add(page)
    cpu.mem_write(va,elf[off:off+filesz])
rom_bytes=(ROOT/'build/roms/europe.nds').read_bytes()
assert zlib.crc32(rom_bytes)==0x29715dec,'Requires verified ASMP0 source ROM'
rom=ndspy.rom.NintendoDSRom(rom_bytes)
arm9=ndspy.codeCompression.decompress(rom.arm9)
cpu.mem_map(0x02052000,4096,5)
original_ranges=((0x02052800,0x20),(0x02052820,0x1c),(0x0205283c,0x1c))
for address,size in original_ranges:
    offset=address-rom.arm9RamAddress
    cpu.mem_write(address,bytes(arm9[offset:offset+size]))
buffer=0x500000;stack=0x600000;stop=0x700000
cpu.mem_map(buffer,4096,3);cpu.mem_map(stack,4096,3);cpu.mem_map(stop,4096,5)
saved=(UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,
    UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11)
def invoke(address,sine,cosine):
    cpu.mem_write(buffer,bytes([0xa5])*64)
    cpu.reg_write(UC_ARM_REG_CPSR,0x10)
    for reg,value in ((UC_ARM_REG_R0,buffer+8),(UC_ARM_REG_R1,sine),
                      (UC_ARM_REG_R2,cosine),(UC_ARM_REG_SP,stack+0xf00),(UC_ARM_REG_LR,stop)):
        cpu.reg_write(reg,value)
    for i,reg in enumerate(saved): cpu.reg_write(reg,0x12340000+i)
    cpu.emu_start(address,stop,count=1000)
    assert cpu.reg_read(UC_ARM_REG_PC)==stop
    assert cpu.reg_read(UC_ARM_REG_SP)==stack+0xf00
    assert all(cpu.reg_read(reg)==0x12340000+i for i,reg in enumerate(saved))
    result=bytes(cpu.mem_read(buffer,64))
    assert result[:8]==bytes([0xa5])*8 and result[56:]==bytes([0xa5])*8
    return result[8:56]

edge=(0,1,4096,0xfffff000,2896,0x80000000,0x7fffffff,0xffffffff)
cases=[(a,b) for a in edge for b in edge]
rng=random.Random(0x52820)
cases.extend((rng.getrandbits(32),rng.getrandbits(32)) for _ in range(1000))
counts={}
for name,(original,_) in zip(NAMES,original_ranges):
    for sine,cosine in cases:
        native=invoke(symbols[name],sine,cosine)
        oracle=invoke(original|1,sine,cosine)
        assert native==oracle,(name,sine,cosine,native.hex(),oracle.hex())
    counts[name]=len(cases)
report={'status':'passed','scope':'Unicorn CPU leaf comparison; no 3DS, gameplay or timing proof',
    'native_object_sha256':hashlib.sha256(obj.read_bytes()).hexdigest(),
    'native_elf_sha256':hashlib.sha256(elf).hexdigest(),
    'rom_sha256':hashlib.sha256(rom_bytes).hexdigest(),'comparisons':counts,
    'total_comparisons':sum(counts.values()),'canaries_and_calling_convention':'passed'}
(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
